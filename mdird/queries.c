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
	char reply[512];
	size_t len = snprintf(reply, sizeof(reply), "%lld %s %d %s\n", seq, cmd_name, status, cmpl);
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
		subscription_del(sub);
		answer(env, seq, "UNSUB", 200, "OK");
	}
}

/*
 * PUT/REM
 */

void exec_put(struct cnx_env *env, long long seq, char const *dir)
{
	struct header *h;
	h = header_new();
	on_error return;
	int status = 200;
	header_read(h, env->fd);
	mdir_version version;
	on_error {
		status = 502;
	} else {
		header_debug(h);
		struct mdir *mdir = mdir_lookup(dir);
		on_error {
			status = 502;
		} else {
			version = mdir_patch_add(mdir, h);
			on_error status = 502;
		}
	}
	header_del(h);
	answer(env, seq, "PUT", status, status == 200 ? mdir_version2str(version) : (is_error() ? error_str():"Error"));
	error_clear();
}

void exec_rem(struct cnx_env *env, long long seq, char const *dir, char const *key)
{
	struct mdir *mdir = mdir_lookup(dir);
	on_error {
		answer(env, seq, "REM", 501, error_str());
		error_clear();
		return;
	}
	mdir_version to_del = mdir_str2version(key);
	on_error {
		answer(env, seq, "REM", 501, error_str());
		error_clear();
		return;
	}
	mdir_version version = mdir_patch_del(mdir, to_del);
	on_error {
		answer(env, seq, "REM", 501, error_str());
		error_clear();
		return;
	}
	answer(env, seq, "REM", 200, mdir_version2str(version));
}

/*
 * Quit
 */

void exec_quit (struct cnx_env *env, long long seq)
{
	debug("doing QUIT");
	answer(env, seq, "QUIT", 200, "OK");
}
