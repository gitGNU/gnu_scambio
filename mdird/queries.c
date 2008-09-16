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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pth.h>
#include "scambio.h"
#include "mdird.h"
#include "misc.h"
#include "varbuf.h"
#include "scambio/header.h"
#include "scambio/mdir.h"
#include "sub.h"

/*
 * Init
 */


void exec_begin(void)
{
}

void exec_end(void)
{
}

/*
 * Answers
 */

static void answer(struct cnx_env *env, long long seq, char const *cmd_name, int status, char const *cmpl)
{
	char reply[100];
	size_t len = snprintf(reply, sizeof(reply), "%lld %s %d %s\n", seq, cmd_name, status, cmpl);
	assert(len < sizeof(reply));
	Write(env->fd, reply, len);
}

/*
 * Subscriptions
 */

void exec_sub(struct cnx_env *env, long long seq, char const *dir, mdir_version version)
{
	debug("doing SUB for '%s', last version %"PRIversion, dir, version);
	int substatus = 0;
	// Check if we are already registered
	struct subscription *sub = subscription_find(env, dir);
	if (sub) {
		subscription_reset_version(sub, version);
		substatus = 1;	// signal that it's a reset
	} else do {
		sub = subscription_new(env, dir, version);
		on_error {
			substatus = 3;	// we faulted
			break;
		}
	} while(0);
	answer(env, seq, "SUB", (is_error() ? 500:200)+substatus, is_error() ? error_str():"OK");
	error_clear();	// error dealt with
}

void exec_unsub(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing UNSUB for '%s'", dir);
	struct subscription *sub = subscription_find(env, dir);
	if (! sub) {
		answer(env, seq, "UNSUB", 501, "Not subscribed");
	} else {
		(void)pth_cancel(sub->thread_id);	// better set the cancellation type to PTH_CANCEL_ASYNCHRONOUS
		LIST_REMOVE(sub, env_entry);
		subscription_del(sub);
		answer(env, seq, "UNSUB", 200, "OK");
	}
}

/*
 * PUT/REM
 */

static bool is_directory(struct header *h) {
	char const *type = header_search(h, SCAMBIO_TYPE_FIELD);
	return type && 0 == strncmp("dir", type, 3);
}

// dir is the directory user name instead of dirId, because we wan't the client
// to be able to add things to this directory before knowing it's dirId.
static void add_header(char const *dir, struct header *h, enum mdir_action action)
{
	struct mdir *mdir = mdir_lookup(dir);
	on_error return;
	if (is_directory(h)) {
		struct mdir *subdir;
		char const *dirid_str = header_search(h, SCAMBIO_DIRID_FIELD);
		char const *dirname   = header_search(h, SCAMBIO_NAME_FIELD);
		debug("header is for directory id=%s, name=%s", dirid_str, dirname);
		if (action == '+') {
			if (! dirname) dirname = "Unnamed";
			if (dirid_str) with_error(0, "DirId already set") return;
			subdir = mdir_new();
			on_error return;	
			header_add_field(h, SCAMBIO_DIRID_FIELD, mdir_id(mdir));
			on_error return;
			mdir_link(mdir, dirname, subdir);
			on_error return;
		} else {
			if (! dirname) with_error(0, "folder name ommited") return;
			mdir_unlink(mdir, dir);
			on_error return;
		}
	}
	// then write the patch
	mdir_patch(mdir, action, h);
}

static void exec_putrem(char const *cmdtag, enum mdir_action action, struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing %s in '%s'", cmdtag, dir);
	struct header *h;
	h = header_new();
	on_error return;
	int status = 200;
	header_read(h, env->fd);
	on_error {
		status = 502;
	} else {
		header_debug(h);
		add_header(dir, h, action);
		on_error status = 502;
	}
	header_del(h);
	answer(env, seq, cmdtag, status, is_error() ? error_str():"OK");
	error_clear();
}

void exec_put(struct cnx_env *env, long long seq, char const *dir)
{
	exec_putrem("PUT", MDIR_ADD, env, seq, dir);
}

void exec_rem(struct cnx_env *env, long long seq, char const *dir)
{
	exec_putrem("REM", MDIR_REM, env, seq, dir);
}

/*
 * Quit
 */

void exec_quit (struct cnx_env *env, long long seq)
{
	debug("doing QUIT");
	answer(env, seq, "QUIT", 200, "OK");
}
