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
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "main.h"
#include "scambio.h"
#include "cmd.h"
#include "header.h"
#include "persist.h"
#include "digest.h"

/* The reader listens for commands.
 * On a put response, it removes the temporary filename stored in the action
 * (the actual meta file will be synchronized independantly from the server).
 * On a rem response, it removes the temporary filename (same remark as above).
 * On a sub response, it moves the subscription from subscribing to subscribed.
 * On an unsub response, it deletes the unsubcribing subscription.
 * On a patch for addition, it creates the meta file (under the digest name)
 * and updates the version number. The meta may already been there if the update of
 * the version number previously failed.
 * On a patch for deletion, it removes the meta file and updates the version number.
 * Again, the meta may already have been removed if the update of the version number
 * previously failed.
 */

static bool terminate_reader;

int finalize_sub(struct command *cmd, int status)
{
	if (! cmd) return -ENOENT;
	if (status != 200) {
		error("Cannot subscribe to directory '%s' : error %d", cmd->path, status);
		command_del(cmd);
		return -EINVAL;
	}
	command_change_list(cmd, &subscribed);
	return 0;
}

int finalize_unsub(struct command *cmd, int status)
{
	if (! cmd) return -ENOENT;
	if (status != 200) {
		error("Cannot unsubscribe from directory '%s' : error %d", cmd->path, status);
		command_del(cmd);
		return -EINVAL;
	}
	command_del(cmd);
	return 0;
}

int finalize_put(struct command *cmd, int status)
{
	if (! cmd) return -ENOENT;
	if (status != 200) {
		error("Cannot put file '%s' : error %d", cmd->path, status);
		command_del(cmd);
		return -EINVAL;
	}
	int err = 0;
	if (0 != unlink(cmd->path)) {
		error("Cannot unlink file '%s' : %s", cmd->path, strerror(errno));
		err = -errno;
	}
	command_del(cmd);
	return err;
}

int finalize_rem(struct command *cmd, int status)
{
	return finalize_put(cmd, status);
}

int finalize_class(struct command *cmd, int status)
{
	(void)cmd;
	(void)status;
	// TODO
	return -ENOSYS;
}

int finalize_quit(struct command *cmd, int status)
{
	(void)cmd;
	(void)status;
	terminate_reader = true;
	return 0;
}

struct patch {
	LIST_ENTRY(patch) entry;
	long long old_version, new_version;
	struct header *header;
	char action;
};
// One per directory
struct mdir {
	LIST_ENTRY(mdir) entry;
	char path[PATH_MAX];	// FIXME: use either dirId or inode number rather than path to identify dir
	LIST_HEAD(patches, patch) patches;
	struct persist stored_version;
};
// List of patched folders lists
static LIST_HEAD(mdirs, mdir) mdirs;

static int patch_ctor(struct patch *patch, struct mdir *mdir, long long old_version, long long new_version, char action)
{
	int err = 0;
	patch->old_version = old_version;
	patch->new_version = new_version;
	patch->action = action;
	if (0 != (err = header_read(patch->header, cnx.sock_fd))) goto q0;
	// Insert this patch into this mdir
	struct patch *p, *prev_p = NULL;
	LIST_FOREACH(p, &mdir->patches, entry) {
		if (p->old_version > old_version) break;
		prev_p = p;
	}
	if (! prev_p) {
		LIST_INSERT_HEAD(&mdir->patches, patch, entry);
	} else {
		LIST_INSERT_AFTER(prev_p, patch, entry);
	}
	return 0;
	header_del(patch->header);
q0:
	return err;
}

static void patch_dtor(struct patch *patch)
{
	LIST_REMOVE(patch, entry);
	header_del(patch->header);
}

static void patch_del(struct patch *patch)
{
	patch_dtor(patch);
	free(patch);
}

static int mdir_ctor(struct mdir *mdir, char const *folder)
{
	int err = 0;
	LIST_INIT(&mdir->patches);
#	define VERSION_FNAME ".version"
#	define VERSION_FNAME_LEN 8
	size_t len = snprintf(mdir->path, sizeof(mdir->path), "%s/%s/"VERSION_FNAME, root_dir, folder);
	if (0 != (err = persist_ctor(&mdir->stored_version, sizeof(long long), mdir->path))) return err;
	mdir->path[len - (VERSION_FNAME_LEN+1)] = '\0';
	LIST_INSERT_HEAD(&mdirs, mdir, entry);
	return 0;
}
static void mdir_dtor(struct mdir *mdir)
{
	struct patch *patch;
	while (NULL != (patch = LIST_FIRST(&mdir->patches))) {
		patch_del(patch);
	}
	LIST_REMOVE(mdir, entry);
	persist_dtor(&mdir->stored_version);
}
static int mdir_new(struct mdir **mdir, char const *folder)
{
	int err = 0;
	*mdir = malloc(sizeof(**mdir));
	if (! *mdir) return -ENOMEM;
	if (0 != (err = mdir_ctor(*mdir, folder))) {
		free(*mdir);
		*mdir = NULL;
	}
	return err;
}
static void mdir_del(struct mdir *mdir)
{
	mdir_dtor(mdir);
	free(mdir);
}
static long long *mdir_version(struct mdir *mdir)
{
	return mdir->stored_version.data;
}

static int mdir_get(struct mdir **mdir, char const *folder)
{
	LIST_FOREACH(*mdir, &mdirs, entry) {
		if (0 == strcmp(folder, (*mdir)->path+root_dir_len+1)) break;
	}
	if (*mdir) return 0;
	return mdir_new(mdir, folder);
}

bool mdir_exists(char const *folder)
{
	struct mdir *dummy;
	int err = mdir_get(&dummy, folder);
	return err == 0;
}

static int mdir_add_header(struct mdir *mdir, struct header *h)
{
	int err = 0;
	char name[PATH_MAX];
	size_t len = snprintf(name, sizeof(name), "%s/.meta/", mdir->path);
	if (len + MAX_DIGEST_STRLEN >= sizeof(name)) return -ENAMETOOLONG;
	if (0 != (err = header_digest(h, MAX_DIGEST_STRLEN+1, name+len))) return err;
	int fd = open(name, O_RDWR|O_CREAT|O_EXCL);
	if (fd < 0) return -errno;
	err = header_write(h, fd);
	if (0 != close(fd) && !err) err = -errno;
	// And then, based on content-type, advertise this new message
	return err;
}

static int mdir_try_apply(struct mdir *mdir)
{
	int err = 0;
	struct patch *patch;
	while (NULL != (patch = LIST_FIRST(&mdir->patches)) && *mdir_version(mdir) == patch->old_version) {
		if (0 != (err = mdir_add_header(mdir, patch->header)) && err != -EEXIST) break;
		*mdir_version(mdir) = patch->new_version;
		patch_del(patch);
	}
	return err;
}

static int patch_fetch(struct mdir *mdir, long long old_version, long long new_version, char action)
{
	debug("fetching patch : do %c for %lld->%lld of '%s'", action, old_version, new_version, mdir->path);
	int err = 0;
	struct patch *patch = malloc(sizeof(*patch));
	if (! patch) return -ENOMEM;
	if (0 != (err = patch_ctor(patch, mdir, old_version, new_version, action))) {
		free(patch);
		return err;
	}
	return 0;
}

static char const *const kw_patch = "patch";

void *reader_thread(void *args)
{
	(void)args;
	int err = 0;
	debug("starting reader thread");
	terminate_reader = false;
	do {
		// read and parse one line of input
		struct cmd cmd;
		if (0 != (err = cmd_read(&cmd, cnx.sock_fd))) break;
		for (unsigned t=0; t<sizeof_array(command_types); t++) {
			if (cmd.keyword == kw_patch) {
				struct mdir *mdir;
				err = mdir_get(&mdir, cmd.args[0].val.string);
				if (! err) err = patch_fetch(mdir, cmd.args[1].val.integer, cmd.args[2].val.integer, cmd.args[3].val.string[0]);
				if (! err) err = mdir_try_apply(mdir);
			}
			if (cmd.keyword == command_types[t].keyword) {
				struct command *command = NULL;
				(void)pth_rwlock_acquire(&command_types_lock, PTH_RWLOCK_RW, FALSE, NULL);
				(void)command_get_by_seqnum(&command, &command_types[t].list, cmd.seq);
				err = command_types[t].finalize(command, cmd.args[0].val.integer);
				(void)pth_rwlock_release(&command_types_lock);
				break;
			}
		}
		cmd_dtor(&cmd);
	} while (! terminate_reader);
	if (err) error("reader terminated : %s", strerror(-err));
	return NULL;
}

int reader_begin(void)
{
	for (unsigned t=0; t<sizeof_array(command_types); t++) {
		cmd_register_keyword(command_types[t].keyword,  0, UINT_MAX, CMD_EOA);
	}
	cmd_register_keyword(kw_patch, 4, 4, CMD_STRING, CMD_INTEGER, CMD_INTEGER, CMD_STRING, CMD_EOA);
	LIST_INIT(&mdirs);
	return 0;
}

void reader_end(void)
{
	for (unsigned t=0; t<sizeof_array(command_types); t++) {
		cmd_unregister_keyword(command_types[t].keyword);
	}
	cmd_unregister_keyword(kw_patch);
	// Free all patches
	struct mdir *mdir;
	while (NULL != (mdir = LIST_FIRST(&mdirs))) {
		mdir_del(mdir);
	}
}

