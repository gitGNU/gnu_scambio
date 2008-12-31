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
extern inline unsigned mdirb_size(struct mdirb *);
extern inline void mdirb_listener_ctor(struct mdirb_listener *, struct mdirb *, void (*)(struct mdirb_listener *, struct mdirb *));
extern inline void mdirb_listener_dtor(struct mdirb_listener *);

/*
 * Refresh an mdir msg list & count.
 * Note: Cannot do that while in mdir_alloc because the mdir is not usable yet.
 */

static void rem_msg(struct mdir *mdir, mdir_version version, void *data)
{
	bool *changed = (bool *)data;
	struct mdirb *mdirb = mdir2mdirb(mdir);
	struct sc_msg *msg;
	debug("searching version %"PRIversion" amongst messages", version);
	LIST_FOREACH(msg, &mdirb->msgs, entry) {	// TODO: hash me using version please
		if (msg->version == version) {
			debug("...found!");
			LIST_REMOVE(msg, entry);
			msg->mdirb->nb_msgs --;
			sc_msg_unref(msg);
			*changed = true;
			break;
		}
	}
	debug("nb_msgs in %s is now %u", mdirb->mdir.path, mdirb->nb_msgs);
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

