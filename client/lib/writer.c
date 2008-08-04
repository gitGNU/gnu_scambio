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
#include "main.h"

/* A run goes like this :
 * - wait until root queue not empty ;
 * - unqueue the oldest one, and process the directory recursively :
 *   - read its .hide file and parse it ;
 *   - For each folder entry :
 *     - if the entry is a directory :
 *       - if we are in a '.put' or a '.rem' directory, ignore it ;
 *       - if it's a .put directory, recurse with flag 'in_put' set ;
 *       - if it's a .rem directory, recurse with flag 'in_rem' set ;
 *       - if it's not on the hide list :
 *         - if it's not already subscribed, or if the subscription command timed out,
 *           subscribe to it ;
 *         - anyway, recurse.
 *       - so this is on the hide list :
 *         - if we are tracking this folder, or if the pending unsubscription timed out,
 *           send an unsub command _and_recurse_ ;
 *     - otherwise it's an ordinary file :
 *       - if we are neither in a '.put' not a '.rem' directory, ignore it ;
 *       - send the appropriate command if it was not already sent or if
 *         the previous one timed out ;
 * After each entry is inserted a schedule point so other threads can execute.
 * Also, for security, a max depth is fixed to 30.
 */
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

enum dir_type { PUT_DIR, REM_DIR, FOLDER_DIR };
bool terminate_writer;

static int try_subscribe(char const *path)
{
	int err = 0;
	assert(path);
	subscription_lock();
	do {
		struct command *subscription = subscription_get(path);
		if (subscription) break;
		subscription = subscription_get_pending(path);
		if (subscription) break;
		if (0 != (err = subscription_new(&subscription, path))) break;
		if (0 != (err = send_command("SUB", path, NULL))) {
			command_del(subscription);
			break;
		}
	} while (0);
	subscription_unlock();
	return err;
}
static int try_unsubscribe(char const *path)
{
	(void)path;
	return 0;
}
static int try_command(enum dir_type dir_type, char const *path)
{
	(void)dir_type;
	(void)path;
	return 0;
}

static bool always_skip(char const *name)
{
	return
		0 == strcmp(name, ".") ||
		0 == strcmp(name, "..");
}

static char const *dirtype2str(enum dir_type dir_type)
{
	switch (dir_type) {
		case PUT_DIR:    return "PUT";
		case REM_DIR:    return "REM";
		case FOLDER_DIR: return "NORMAL";
	}
	return "INVALID";
}

#define PUTDIR_NAME ".put"
#define REMDIR_NAME ".rem"
static int writer_run_rec(char path[], enum dir_type dir_type, int depth)
{
#	define MAX_DEPTH 30
	debug("run on path %s, dirtype %s", path, dirtype2str(dir_type));
	if (depth >= MAX_DEPTH) return -ELOOP;
	int err;
	struct hide_cfg *hide_cfg;
	if (0 != (err = hide_cfg_get(&hide_cfg, path))) goto q0;	// load the .hide file
	DIR *dir = opendir(path);
	if (! dir) {
		err = -errno;
		goto q1;
	}
	struct dirent *dirent;
	errno = 0;
	while (!err && NULL != (dirent = readdir(dir))) {
		if (always_skip(dirent->d_name)) continue;
		pth_yield(NULL);
		struct stat statbuf;
		if (0 != stat(dirent->d_name, &statbuf)) {
			err = -errno;
			break;
		}
		path_push(path, dirent->d_name);
		if (S_ISDIR(statbuf.st_mode)) {
			if (dir_type == PUT_DIR || dir_type == REM_DIR) continue;
			if (0 == strcmp(dirent->d_name, PUTDIR_NAME)) {
				err = writer_run_rec(path, PUT_DIR, depth+1);
			} else if (0 == strcmp(dirent->d_name, REMDIR_NAME)) {
				err = writer_run_rec(path, REM_DIR, depth+1);
			} else if (show_this_dir(hide_cfg, dirent->d_name)) {
				err = try_subscribe(path);
				if (! err) err = writer_run_rec(path, FOLDER_DIR, depth+1);
			} else {	// hide this dir
				err = try_unsubscribe(path);
				if (! err) err = writer_run_rec(path, FOLDER_DIR, depth+1);
			}
		} else {	// dir entry is not itself a directory
			if (dir_type == FOLDER_DIR) continue;
			err = try_command(dir_type, path);
		}
		path_pop(path);
	}
	if (!err && errno) {
		err = -errno;
	}
	closedir(dir);
q1:
	hide_cfg_release(hide_cfg);
q0:
	return err;
}

#include <signal.h>
static void wait_signal(void)
{
	int err, sig;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGHUP);
	if (0 != (err = pth_sigwait(&set, &sig))) {	// this is a cancel point
		error("Cannot sigwait : %d", err);
	}
}

void *writer_thread(void *args)
{
	(void)args;
	debug("starting writer thread");
	struct run_path *rp;
	terminate_writer = false;
	do {
		while (NULL != (rp = shift_run_queue())) {
			(void)writer_run_rec(rp->root, FOLDER_DIR, 0);	// FIXME: ensure that we only push folders !
			run_path_del(rp);
		}
		wait_signal();
	} while (! terminate_writer);
	return NULL;
}

int writer_begin(void)
{
	return hide_begin();
}

void writer_end(void)
{
	hide_end();
}
