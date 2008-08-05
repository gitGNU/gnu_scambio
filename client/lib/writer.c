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

#define PUTDIR_NAME ".put"
#define REMDIR_NAME ".rem"
enum dir_type { PUT_DIR, REM_DIR, FOLDER_DIR };

static char const *dirtype2str(enum dir_type dir_type)
{
	switch (dir_type) {
		case PUT_DIR:    return "PUT";
		case REM_DIR:    return "REM";
		case FOLDER_DIR: return "NORMAL";
	}
	return "INVALID";
}

bool terminate_writer;

static int try_command(char const *path, struct commands *list, char const *token, pth_rwlock_t *lock)
{
	int err = 0;
	assert(path);
	if (path[0] == '\0') path = "/";
	debug("try_command(%s on '%s')", token, path);
	(void)pth_rwlock_acquire(lock, PTH_RWLOCK_RW, FALSE, NULL);
	do {
		struct command *dummy;
		if (-ENOENT != (err = command_get(&dummy, list, path, true))) break;
		err = command_new(&dummy, list, path, token);
	} while (0);
	(void)pth_rwlock_release(lock);
	return err;
}
// Path is a file (relative to root_dir, with PUTDIR_NAME) that must be added
static int try_put(char const *path)
{
	int err = 0;
	char folder[PATH_MAX];
	int fd;
	assert(path);
	debug("try_put('%s')", path);
	snprintf(folder, sizeof(folder), "%s/%s", root_dir, path);
	fd = open(folder, O_RDONLY);
	if (fd < 0) return -errno;
	do {
		path_pop(folder);	// chop filename
		path_pop(folder);	// chop PUTDIR_NAME
		assert(strlen(folder) >= root_dir_len);
		if (0 != (err = try_command(folder+root_dir_len, &pending_puts, " PUT ", &pending_puts_lock))) break;
		if (0 != (err = Copy(cnx.sock_fd, fd))) break;
		if (0 != (err = Write(cnx.sock_fd, "::\n", 3))) break;
	} while (0);
	(void)close(fd);
	return err;
}

// Path is the file to be removed, which name the meta's digest
static int try_rem(char const *path)
{
	int err = 0;
	char folder[PATH_MAX];
	assert(path);
	size_t path_len = strlen(path);
	char const *digest = path+path_len;
	while (digest > path && *digest != '/') digest--;
	snprintf(folder, sizeof(folder), "%s", path);
	path_pop(folder);	// chop filename
	path_pop(folder); // chop PUTREM_NAME
	do {
		if (0 != (err = try_command(folder, &pending_rems, " REM ", &pending_rems_lock))) break;
		if (0 != (err = Write_strs(cnx.sock_fd, "digest: ", digest, "\n::\n"))) break;
	} while (0);
	return err;
}

static bool is_already_subscribed(char const *path)
{
	struct command *dummy;
	(void)pth_rwlock_acquire(&subscriptions_lock, PTH_RWLOCK_RW, FALSE, NULL);
	int err = command_get(&dummy, &subscribed, path, false);
	(void)pth_rwlock_release(&subscriptions_lock);
	return err == 0;
}

static bool always_skip(char const *name)
{
	return
		0 == strcmp(name, ".") ||
		0 == strcmp(name, "..");
}

static int writer_run_rec(char path[], enum dir_type dir_type, int depth)
{
#	define MAX_DEPTH 30
	debug("writer_run_rec(path=%s, dir_type=%s)", path, dirtype2str(dir_type));
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
				if (! is_already_subscribed(path)) {
					err = try_command(path, &subscribing, " SUB ", &subscriptions_lock);
					if (-EINPROGRESS == err) err = 0;
					if (! err) err = writer_run_rec(path, FOLDER_DIR, depth+1);
				}
			} else {	// hide this dir
				if (is_already_subscribed(path)) {
					err = try_command(path, &unsubscribing, " UNSUB ", &subscriptions_lock);
					if (-EINPROGRESS == err) err = 0;
					if (! err) err = writer_run_rec(path, FOLDER_DIR, depth+1);
				}
			}
		} else {	// dir entry is not itself a directory
			switch (dir_type) {
				case FOLDER_DIR: break;
				case PUT_DIR:    err = try_put(path); break;
				case REM_DIR:    err = try_rem(path); break;
			}
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
			int err = writer_run_rec(rp->root, FOLDER_DIR, 0);	// root is the full path
			if (err) warning("While scanning root dir : %s", strerror(-err));
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
