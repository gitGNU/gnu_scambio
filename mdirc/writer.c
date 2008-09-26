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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pth.h>
#include "misc.h"
#include "hide.h"
#include "scambio.h"
#include "mdirc.h"
#include "scambio/header.h"

static bool terminate_writer;

#include <signal.h>
static void wait_signal(void)
{
	debug("wait signal");
	int err, sig;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	if (0 != (err = pth_sigwait(&set, &sig))) {	// this is a cancel point
		error("Cannot sigwait : %d", err);
	}
	debug("got signal");
}

static void parse_dir_rec(struct mdirc *mdirc, enum mdir_action action, bool synched, mdir_version version);
static void ls_patch(struct mdir *mdir, struct header *header, enum mdir_action action, bool synched, union mdir_list_param param)
{
	struct mdirc *mdirc = mdir2mdirc(mdir);
	if (! synched) {	// not in journal and not already acked
		// but may already have been sent nonetheless.
		enum command_type type = action == MDIR_ADD ? PUT_CMD_TYPE:REM_CMD_TYPE;
		struct command *cmd = command_get_by_path(mdirc, type, param.path);
		if (! cmd) {
			(void)command_new(type, mdirc, mdir_id(mdir), param.path);
			on_error return;
		}
	}
	// Anyway, if it is a directory, recurse
	if (header_is_directory(header)) {
		// find this dirId, which may be transient... so not in the header !
		// We definitively needs a distinct folder ls, which would list symlinks instead of journal,
		// and provide the dirId ! mdir_list still list the patches, so the dirId patch as well.
		//parse_dir_rec(mdirc, action, synched, param.version);
		on_error return;
	}
}

// Subscribe to the directory, then scan it
static void parse_dir_rec(struct mdirc *mdirc, enum mdir_action action, bool synched, mdir_version version)
{
	debug("parsing directory '%s'", mdir_id(&mdirc->mdir));
	(void)synched;
	(void)version;
	// Subscribe to the directory if its not already done
	if (!mdirc->subscribed && action == MDIR_ADD && synched && !mdir_is_transient(&mdirc->mdir)) {
		// This is not enough to be synched : we must ensure that we have received the patch yet
		// FIXME: we need 3 states not two : not acked, acked and synched (the 4th state, 'sent',
		// does not survive to program restart, so we must handle it conservatively).
		debug("subscribing to dir %s", mdir_id(&mdirc->mdir));
		// this is not fatal to subscribe twice to a dirId, but better avoid it
		struct command *cmd = command_get_by_path(mdirc, SUB_CMD_TYPE, "");
		if (cmd) {
			debug("already subscribing to %s", mdir_id(&mdirc->mdir));
		} else {
			(void)command_new(SUB_CMD_TYPE, mdirc, mdir_id(&mdirc->mdir), "");
		}
		on_error return;
	}
	// List its content
	mdir_patch_list(&mdirc->mdir, true, true, ls_patch);
}

void *writer_thread(void *args)
{
	(void)args;
	debug("starting writer thread");
	terminate_writer = false;
	do {
		struct mdir *root = mdir_lookup("/");
		on_error break;
		parse_dir_rec(mdir2mdirc(root), MDIR_ADD, true, 0);
		on_error break;
		wait_signal();
	} while (! terminate_writer);
	return NULL;
}

void writer_begin(void)
{
	hide_begin();
}

void writer_end(void)
{
	hide_end();
}
