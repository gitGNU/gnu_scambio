/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <string.h>
#include "scambio.h"
#include "mdirb.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "scambio/timetools.h"
#include "misc.h"
#include "merelib.h"
#include "vadrouille.h"
#include "mdirb.h"

/*
 * MDir allocator
 */

static struct mdir *mdirb_alloc(void)
{
	struct mdirb *mdirb = Malloc(sizeof(*mdirb));
	LIST_INIT(&mdirb->msgs);
	LIST_INIT(&mdirb->listeners);
	mdirb->nb_msgs = 0;
	mdirb->nb_unread = 0;
	mdir_cursor_ctor(&mdirb->cursor);
	return &mdirb->mdir;
}

static void mdirb_free(struct mdir *mdir)
{
	struct mdirb *mdirb = mdir2mdirb(mdir);
	
	struct sc_msg *msg;
	while (NULL != (msg = LIST_FIRST(&mdirb->msgs))) {
		LIST_REMOVE(msg, entry);
		mdirb->nb_msgs --;
		sc_msg_unref(msg);
	}
	assert(mdirb->nb_msgs == 0);
	
	struct mdirb_listener *listener;
	while (NULL != (listener = LIST_FIRST(&mdirb->listeners))) {
		LIST_REMOVE(listener, entry);
	}
	
	mdir_cursor_dtor(&mdirb->cursor);
	free(mdirb);
}

extern inline struct mdirb *mdir2mdirb(struct mdir *);
extern inline void mdirb_listener_ctor(struct mdirb_listener *, struct mdirb *, void (*)(struct mdirb_listener *, struct mdirb *));
extern inline void mdirb_listener_dtor(struct mdirb_listener *);

/*
 * Global Notifications
 */

static LIST_HEAD(listeners, sc_msg_listener) listeners = LIST_HEAD_INITIALIZER(&listeners);

void sc_msg_listener_ctor(struct sc_msg_listener *listener, void (*cb)(struct sc_msg_listener *, struct mdirb *, enum mdir_action, struct sc_msg *))
{
	listener->cb = cb;
	LIST_INSERT_HEAD(&listeners, listener, entry);
}

void sc_msg_listener_dtor(struct sc_msg_listener *listener)
{
	LIST_REMOVE(listener, entry);
}

static void notify(struct mdirb *mdirb, enum mdir_action action, struct sc_msg *msg)
{
	struct sc_msg_listener *listener, *tmp;
	LIST_FOREACH_SAFE(listener, &listeners, entry, tmp) {
		listener->cb(listener, mdirb, action, msg);
	}
}

/*
 * Refresh an mdir msg list & count.
 * Note: Cannot do that while in mdir_alloc because the mdir is not usable yet.
 */

static struct sc_msg *find_msg_by_version(struct mdirb *mdirb, mdir_version version)
{
	debug("searching version %"PRIversion" amongst messages", version);
	struct sc_msg *msg;
	LIST_FOREACH(msg, &mdirb->msgs, entry) {	// TODO: hash me using version please
		if (msg->version == version) {
			debug("...found!");
			return msg;
		}
	}
	return NULL;
}

static void rem_msg(struct mdir *mdir, mdir_version version, void *data)
{
	bool *changed = (bool *)data;
	struct mdirb *mdirb = mdir2mdirb(mdir);

	struct sc_msg *msg = find_msg_by_version(mdirb, version);
	if (! msg) return;

	// Remove it
	LIST_REMOVE(msg, entry);
	msg->mdirb->nb_msgs --;
	if (! msg->was_read) msg->mdirb->nb_unread --;
	notify(mdirb, MDIR_REM, msg);
	sc_msg_unref(msg);
	*changed = true;
	
	debug("nb_msgs in %s is now %u (%u unread)", mdirb->mdir.path, mdirb->nb_msgs, mdirb->nb_unread);
}

static void add_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	struct mdirb *mdirb = mdir2mdirb(mdir);
	bool *changed = (bool *)data;

	if (header_is_directory(h)) return;

	debug("try to add msg version %"PRIversion, version);
	*changed = true;
	struct sc_msg *msg = NULL;
	struct sc_plugin *plugin;

	// We handle internally the have_read mark
	if (header_has_type(h, SC_MARK_TYPE)) {
		struct header_field *hf = NULL;
		char const *username = mdir_user_name(user);
		while (NULL != (hf = header_find(h, SC_HAVE_READ_FIELD, hf))) {
			if (0 == strcmp(username, hf->value)) {
				mdir_version target = header_target(h);
				on_error {
					error_clear();
					return;
				}
				debug("Mark message %"PRIversion" as read", target);
				msg = find_msg_by_version(mdirb, target);
				if (msg && ! msg->was_read) {
					msg->was_read = true;
					mdirb->nb_unread --;
					notify(mdirb, MDIR_REM, msg);
				}
				return;
			}
		}
		// If we are not marked as a reader, just ignore it.
		return;
	}

	struct header_field *type = header_find(h, SC_TYPE_FIELD, NULL);
	// look for an exact match first
	if (type) {
		debug("  try for type '%s'", type->value);
		LIST_FOREACH(plugin, &sc_plugins, entry) {
			debug("    plugin '%s' ?", plugin->name);
			if (! plugin->ops->msg_new) continue;
			if (plugin->type && 0 != strcmp(plugin->type, type->value)) continue;
			if_succeed (msg = plugin->ops->msg_new(mdirb, h, version)) break;
			error_clear();
		}
	}

	// Then try duck-typing
	if (! msg) {
		debug("  try duck typing");
		LIST_FOREACH(plugin, &sc_plugins, entry) {
			debug("    plugin '%s' ?", plugin->name);
			if (! plugin->ops->msg_new) continue;
			if_succeed (msg = plugin->ops->msg_new(mdirb, h, version)) break;
			error_clear();
		}
	}

	assert(msg);	// default plugin should have accepted it
	LIST_INSERT_HEAD(&mdirb->msgs, msg, entry);
	mdirb->nb_msgs ++;
	if (! msg->was_read) mdirb->nb_unread ++;
	notify(mdirb, MDIR_ADD, msg);
}

void mdirb_refresh(struct mdirb *mdirb)
{
	debug("Refreshing mdirb %s", mdirb->mdir.path);
	bool changed = false;
	mdir_patch_list(&mdirb->mdir, &mdirb->cursor, false, add_msg, rem_msg, &changed);
	if (changed) {
		debug("noticing listeners because content changed");
		struct mdirb_listener *listener, *tmp;
		LIST_FOREACH_SAFE(listener, &mdirb->listeners, entry, tmp) {
			listener->refresh(listener, mdirb);
		}
	}
}

/*
 * Init
 */

void mdirb_init(void)
{
	mdir_alloc = mdirb_alloc;
	mdir_free = mdirb_free;
}

