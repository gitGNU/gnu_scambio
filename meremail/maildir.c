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
#include "scambio.h"
#include "maildir.h"
#include "scambio/timetools.h"
#include "scambio/header.h"
#include "misc.h"

/*
 * Data Definitions
 */

//struct maildirs maildirs;

/*
 * Msgs
 */

static void msg_ctor(struct msg *msg, struct maildir *maildir, char const *from, char const *descr, char const *date_str, mdir_version version)
{
	debug("msg from %s, '%s', version %"PRIversion, from, descr, version);
	bool dummy;
	if_fail (msg->date = sc_gmfield2ts(date_str, &dummy)) return;
	if_fail (msg->from = Strdup(from)) return;
	if_fail (msg->descr = Strdup(descr)) {
		free(msg->from);
		return;
	}
	msg->version = version;
	msg->maildir = maildir;
	LIST_INSERT_HEAD(&maildir->msgs, msg, entry);
	maildir->nb_msgs ++;
	
}

struct msg *msg_new(struct maildir *maildir, char const *from, char const *descr, char const *date_str, mdir_version version)
{
	struct msg *msg = malloc(sizeof(*msg));
	if (! msg) with_error(ENOMEM, "malloc(msg)") return NULL;
	if_fail (msg_ctor(msg, maildir, from, descr, date_str, version)) {
		free(msg);
		msg = NULL;
	}
	return NULL;
}

static void msg_dtor(struct msg *msg)
{
	debug("msg '%s'", msg->descr);
	if (msg->from) {
		free(msg->from);
		msg->from = NULL;
	}
	if (msg->descr) {
		free(msg->descr);
		msg->descr = NULL;
	}
	LIST_REMOVE(msg, entry);
	msg->maildir->nb_msgs --;
}

void msg_del(struct msg *msg)
{
	msg_dtor(msg);
	free(msg);
}

/*
 * MsgMdirs
 */

static struct mdir *maildir_alloc(void)
{
	struct maildir *maildir = malloc(sizeof(*maildir));
	if (! maildir) with_error(ENOMEM, "malloc maildir") return NULL;
	LIST_INIT(&maildir->msgs);
	maildir->nb_msgs = 0;
	return &maildir->mdir;
}

static void maildir_free(struct mdir *mdir)
{
	struct maildir *maildir = mdir2maildir(mdir);
	struct msg *msg;
	while (NULL != (msg = LIST_FIRST(&maildir->msgs))) {
		msg_del(msg);
	}
	free(maildir);
}

extern inline struct maildir *mdir2maildir(struct mdir *mdir);

/*
 * Refresh an mdir msg list.
 * Note: Cannot do that while in mdir_alloc because the mdir is not usable yet.
 */

static void rem_msg(struct mdir *mdir, mdir_version version, void *data)
{
	(void)data;
	struct msg *msg;
	struct maildir *maildir = mdir2maildir(mdir);
	debug("searching version %"PRIversion, version);
	LIST_FOREACH(msg, &maildir->msgs, entry) {	// TODO: hash me using version please
		if (msg->version == version) {
			msg_del(msg);
			break;
		}
	}
}

static void add_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	(void)data;
	debug("try to add msg version %"PRIversion, version);
	struct msg *msg;
	struct maildir *maildir = mdir2maildir(mdir);
	// To be a message, a new patch must have a from, descr and start field (duck typing)
	struct header_field *from = header_find(h, SC_FROM_FIELD, NULL);
	if (! from) return;
	struct header_field *descr = header_find(h, SC_DESCR_FIELD, NULL);
	if (! descr) return;
	struct header_field *date = header_find(h, SC_START_FIELD, NULL);
	if (! date) return;
	if_fail (msg = msg_new(maildir, from->value, descr->value, date->value, version)) return;
}

void maildir_refresh(struct maildir *maildir)
{
	debug("Refreshing maildir %s", maildir->mdir.path);
	mdir_patch_list(&maildir->mdir, false, add_msg, rem_msg, NULL);
}

/*
 * Init
 */

void maildir_init(void)
{
//	LIST_INIT(&maildirs);
	mdir_alloc = maildir_alloc;
	mdir_free = maildir_free;
}

char const *ts2staticstr(time_t ts)
{
	static char date_str[64];
	struct tm *tm = localtime(&ts);
	(void)strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm);
	return date_str;
}
