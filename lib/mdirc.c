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
#include <string.h>
#include "scambio.h"
#include "scambio/mdirc.h"
#include "scambio/msg.h"
#include "scambio/timetools.h"
#include "scambio/header.h"
#include "misc.h"

/*
 * Global Notifications
 */

static LIST_HEAD(listeners, sc_msg_listener) listeners = LIST_HEAD_INITIALIZER(&listeners);

void sc_msg_listener_ctor(struct sc_msg_listener *listener, void (*cb)(struct sc_msg_listener *, struct mdirc *, enum mdir_action, struct sc_msg *))
{
	listener->cb = cb;
	LIST_INSERT_HEAD(&listeners, listener, entry);
}

void sc_msg_listener_dtor(struct sc_msg_listener *listener)
{
	LIST_REMOVE(listener, entry);
}

static void notify(struct mdirc *mdirc, enum mdir_action action, struct sc_msg *msg)
{
	struct sc_msg_listener *listener, *tmp;
	debug("Notify for new msg @%p", msg);
	LIST_FOREACH_SAFE(listener, &listeners, entry, tmp) {
		listener->cb(listener, mdirc, action, msg);
	}
}

/*
 * Construct/Destruct
 */

void mdirc_ctor(struct mdirc *mdirc)
{
	LIST_INIT(&mdirc->msgs);
	mdirc->nb_msgs = 0;
	mdir_cursor_ctor(&mdirc->cursor);
}

struct mdir *mdirc_default_alloc(void)
{
	struct mdirc *mdirc = Malloc(sizeof(*mdirc));
	mdirc_ctor(mdirc);
	return &mdirc->mdir;
}

static void mdirc_del_all_msgs(struct mdirc *mdirc)
{
	struct sc_msg *msg;
	while (NULL != (msg = LIST_FIRST(&mdirc->msgs))) {
		LIST_REMOVE(msg, mdirc_entry);
		notify(mdirc, MDIR_REM, msg);
		sc_msg_unref(msg);
	}
	mdirc->nb_msgs = 0;
}

void mdirc_dtor(struct mdirc *mdirc)
{
	mdirc_del_all_msgs(mdirc);
	mdir_cursor_dtor(&mdirc->cursor);
}

void mdirc_default_free(struct mdir *mdir)
{
	struct mdirc *mdirc = mdir2mdirc(mdir);
	mdirc_dtor(mdirc);
	free(mdirc);
}

extern inline struct mdirc *mdir2mdirc(struct mdir *);

/*
 * Refresh an mdir msg list & count.
 * Note: Cannot do that while in mdir_alloc because the mdir is not usable yet.
 */

static struct sc_msg *find_msg_by_version(struct mdirc *mdirc, mdir_version version)
{
	debug("searching version %"PRIversion" amongst messages", version);
	struct sc_msg *msg;
	LIST_FOREACH(msg, &mdirc->msgs, mdirc_entry) {	// TODO: hash me using version please
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
	struct mdirc *mdirc = mdir2mdirc(mdir);

	struct sc_msg *msg = find_msg_by_version(mdirc, version);
	if (! msg) return;

	// Remove it
	LIST_REMOVE(msg, mdirc_entry);
	msg->mdirc->nb_msgs --;
	notify(mdirc, MDIR_REM, msg);
	sc_msg_unref(msg);
	*changed = true;
	
	debug("nb_msgs in %s is now %u", mdirc->mdir.path, mdirc->nb_msgs);
}

static void add_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	struct mdirc *mdirc = mdir2mdirc(mdir);
	bool *changed = (bool *)data;

	if (header_is_directory(h)) return;

	debug("try to add msg version %"PRIversion, version);
	*changed = true;
	struct sc_msg *msg;

	// We handle markers in a special way
	if (header_has_type(h, SC_MARK_TYPE)) {
		mdir_version target = header_target(h);
		on_error {
			error_clear();
			return;	// forget about it
		}
		debug("Add a mark to message %"PRIversion, target);
		msg = find_msg_by_version(mdirc, target);
		if (! msg) {
			debug("No such message");
		} else {
			struct sc_msg *mark;
			if_fail (mark = sc_msg_new(mdirc, h, version)) return;
			LIST_INSERT_HEAD(&msg->marks, mark, marks_entry);	// give the ref to the main msg
		}
	} else {
		if_fail (msg = sc_msg_new(mdirc, h, version)) return;
		LIST_INSERT_HEAD(&mdirc->msgs, msg, mdirc_entry);
		mdirc->nb_msgs ++;
		notify(mdirc, MDIR_ADD, msg);
	}
}

static void err_msg(struct mdir *mdir, struct header *h, void *data)
{
	(void)mdir;
	(void)data;
	struct header_field *hf_status = header_find(h, SC_STATUS_FIELD, NULL);
	struct header_field *hf_type = header_find(h, SC_TYPE_FIELD, NULL);
	error("Folder cannot be patched with message of type %s : %s",
		hf_type ? hf_type->value : "NONE",
		hf_status ? hf_status->value : "Reason unknown");
}

void mdirc_update(struct mdirc *mdirc)
{
	debug("Refreshing mdirc %s", mdirc->mdir.path);
	// First check for error
	mdir_patch_list(&mdirc->mdir, &mdirc->cursor, false, add_msg, rem_msg, err_msg, NULL);
}

