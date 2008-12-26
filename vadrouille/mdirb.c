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
	mdirb->nb_msgs = 0;
	mdir_cursor_ctor(&mdirb->cursor);
	mdirb->name[0] = '\0';
	return &mdirb->mdir;
}

static void mdirb_free(struct mdir *mdir)
{
	struct mdirb *mdirb = mdir2mdirb(mdir);
	struct sc_msg *msg;
	while (NULL != (msg = LIST_FIRST(&mdirb->msgs))) {
		sc_msg_unref(msg);
	}
	mdir_cursor_dtor(&mdirb->cursor);
	free(mdirb);
}

extern inline struct mdirb *mdir2mdirb(struct mdir *);
extern inline unsigned mdirb_size(struct mdirb *);

/*
 * Refresh an mdir msg list & count.
 * Note: Cannot do that while in mdir_alloc because the mdir is not usable yet.
 */

static void rem_msg(struct mdir *mdir, mdir_version version, void *data)
{
	(void)data;
	struct sc_msg *msg;
	struct mdirb *mdirb = mdir2mdirb(mdir);
	debug("searching version %"PRIversion, version);
	LIST_FOREACH(msg, &mdirb->msgs, entry) {	// TODO: hash me using version please
		if (msg->version == version) {
			sc_msg_unref(msg);
			break;
		}
	}
	debug("nb_msgs in %s is now %u", mdirb->mdir.path, mdirb->nb_msgs);
}

static void add_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	(void)data;
	if (header_is_directory(h)) return;
	debug("try to add msg version %"PRIversion, version);
	struct mdirb *mdirb = mdir2mdirb(mdir);
	struct sc_msg *msg;
	struct sc_plugin *plugin;
	struct header_field *type = header_find(h, SC_TYPE_FIELD, NULL);
	if (type) {	// look for an exact match first
		debug("  try for type '%s'", type->value);
		LIST_FOREACH(plugin, &sc_plugins, entry) {
			debug("    plugin '%s' ?", plugin->name);
			if (! plugin->ops->msg_new) continue;
			if (plugin->type && 0 != strcmp(plugin->type, type->value)) continue;
			if_succeed (msg = plugin->ops->msg_new(mdirb, h, version)) return;
			error_clear();
		}
	}
	// Then try duck-typing
	debug("  try duck typing");
	LIST_FOREACH(plugin, &sc_plugins, entry) {
		debug("    plugin '%s' ?", plugin->name);
		if (! plugin->ops->msg_new) continue;
		if_succeed (msg = plugin->ops->msg_new(mdirb, h, version)) return;
		error_clear();
	}
	assert(0);	// the default plugin should accept it
}

void mdirb_refresh(struct mdirb *mdirb)
{
	debug("Refreshing mdirb %s", mdirb->mdir.path);
	mdir_patch_list(&mdirb->mdir, &mdirb->cursor, false, add_msg, rem_msg, NULL);
}

extern inline char const *mdirb_name(struct mdirb *mdirb);

void mdirb_set_name(struct mdirb *mdirb, char const *name)
{
	snprintf(mdirb->name, sizeof(mdirb->name), "%s", name);
}

/*
 * Init
 */

void mdirb_init(void)
{
	mdir_alloc = mdirb_alloc;
	mdir_free = mdirb_free;
}

