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
#include "mdsyncc.h"
#include "scambio/header.h"

static bool terminate_writer;

#include <signal.h>
static void wait_signal(void)
{
	debug("wait signal");
	int sig;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	pth_event_t ev = pth_event(PTH_EVENT_TIME, pth_timeout(2, 0));
	(void)pth_sigwait_ev(&set, &sig, ev);	// this is a cancel point
	pth_event_free(ev, PTH_FREE_THIS);
	debug("got signal");
}

static void ls_patch(struct mdir *mdir, struct header *header, enum mdir_action action, bool new, mdir_version version, void *folder)
{
	assert(new);
	struct mdirc *mdirc = mdir2mdirc(mdir);
	// not in journal and not already acked
	// and as patch_list returns patches only once we a certain we did not sent it already
	char const *kw = action == MDIR_ADD ? kw_put:kw_rem;
	char filename[PATH_MAX];
	snprintf(filename, sizeof(filename), "%s/.tmp/%c%"PRIversion, mdir->path, action == MDIR_ADD ? '+':'-', version);
	// FIXME : grasp cnx write lock
	(void)command_new(kw, mdirc, folder, filename);
	unless_error header_write(header, cnx.fd);
	// FIXME : release cnx write lock
}

// Subscribe to the directory, then scan it
static void parse_dir_rec(struct mdir *parent, struct mdir *mdir, bool new, char const *name, void *parent_path)
{
	(void)parent;
	struct mdirc *mdirc = mdir2mdirc(mdir);
	char path[PATH_MAX];
	Make_path(path, sizeof(path), (char *)parent_path, name, NULL);
	debug("parsing subdirectory '%s' of '%s' (dirId = %s)", name, (char *)parent_path, mdir_id(&mdirc->mdir));
	// Subscribe to the directory if its not already done
	if (!mdirc->subscribed && !new) {
		// This is not enough to be synched : we must ensure that we have received the patch yet
		// (this is not fatal to subscribe twice to a dirId, but better avoid it)
		debug("subscribing to dir %s", mdir_id(&mdirc->mdir));
		struct command *cmd = command_get_by_path(mdirc, kw_sub, "");
		if (cmd) {
			debug("already subscribing to %s", mdir_id(&mdirc->mdir));
		} else {
			(void)command_new(kw_sub, mdirc, mdir_id(&mdirc->mdir), "");
		}
		on_error return;
	}
	// Synchronize up its content
	debug("list patches");
	mdir_patch_list(&mdirc->mdir, true, ls_patch, path);
	on_error return;
	// Recurse
	debug("list folders");
	mdir_folder_list(mdir, false, parse_dir_rec, path);
}

void *writer_thread(void *arg)
{
	(void)arg;
	debug("starting writer thread");
	terminate_writer = false;
	struct mdir *root = mdir_lookup("/");
	// Traverse folders
	do {
		on_error break;
		parse_dir_rec(NULL, root, false, "", "");
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
