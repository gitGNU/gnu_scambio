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
 * Construct/Destruct
 */

void mdirc_ctor(struct mdirc *mdirc)
{
	TAILQ_INIT(&mdirc->msgs);
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
	while (NULL != (msg = TAILQ_FIRST(&mdirc->msgs))) {
		TAILQ_REMOVE(&mdirc->msgs, msg, mdirc_entry);
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
	TAILQ_FOREACH(msg, &mdirc->msgs, mdirc_entry) {	// TODO: hash me using version please
		if (msg->version == version) {
			debug("...found!");
			return msg;
		}
	}
	return NULL;
}

static void rem_msg(struct mdir *mdir, mdir_version version, void *data)
{
	(void)data;
	struct mdirc *mdirc = mdir2mdirc(mdir);

	struct sc_msg *msg = find_msg_by_version(mdirc, version);
	if (! msg) return;

	// Remove it
	TAILQ_REMOVE(&mdirc->msgs, msg, mdirc_entry);
	msg->mdirc->nb_msgs --;
	sc_msg_unref(msg);
	
	debug("nb_msgs in %s is now %u", mdirc->mdir.path, mdirc->nb_msgs);
}

static void add_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	(void)data;
	struct mdirc *mdirc = mdir2mdirc(mdir);

	if (header_is_directory(h)) return;

	debug("try to add msg version %"PRIversion, version);
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
			if_fail ((void)sc_msg_new(mdirc, h, version, 0, msg)) return;
		}
	} else {
		if_fail (msg = sc_msg_new(mdirc, h, version, 0, NULL)) return;
		TAILQ_INSERT_TAIL(&mdirc->msgs, msg, mdirc_entry);
		mdirc->nb_msgs ++;
	}
}

static void err_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	(void)mdir;
	(void)data;
	(void)version;
	struct mdirc *mdirc = mdir2mdirc(mdir);
	struct header_field *hf_status = header_find(h, SC_STATUS_FIELD, NULL);
	struct header_field *hf_type = header_find(h, SC_TYPE_FIELD, NULL);
	error("Folder cannot be patched with message #%"PRIversion" of type %s : %s",
		version,
		hf_type ? hf_type->value : "NONE",
		hf_status ? hf_status->value : "Reason unknown");
	struct sc_msg *msg = find_msg_by_version(mdirc, version);
	if (msg) {
		msg->status = hf_status ? strtol(hf_status->value, NULL, 0) : -1;
	}
}

void mdirc_update(struct mdirc *mdirc)
{
	debug("Refreshing mdirc %s", mdirc->mdir.path);
	// First check for error
	mdir_patch_list(&mdirc->mdir, &mdirc->cursor, false, add_msg, rem_msg, err_msg, NULL);
}

