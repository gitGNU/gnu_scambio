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
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "mdirc.h"
#include "scambio.h"
#include "scambio/cnx.h"
#include "scambio/header.h"
#include "persist.h"
#include "digest.h"

/* The reader listens for command answers.
 * On a put response, it renames the temporary filename stored in the action to
 * "+/-=version" (the actual meta file will be synchronized independantly from
 * the server).  On a rem response, it removes the temporary filename (same
 * remark as above).  On a sub response, it moves the subscription from
 * subscribing to subscribed.  On an unsub response, it deletes the
 * unsubcribing subscription.  On a patch, it adds the patch to the patch ring,
 * and try to reduce it (ie push as much as possible on the mdir journal, while
 * the versions sequence is OK).
 */

static bool terminate_reader;

void finalize_sub(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	int status = cmd->args[0].integer;
	debug("subscribing to %s : %d", mdir_id(&command->mdirc->mdir), status);
	if (status == 200) command->mdirc->subscribed = true;
	command_del(command);
}

void finalize_unsub(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	int status = cmd->args[0].integer;
	debug("unsubscribing to %s : %d", mdir_id(&command->mdirc->mdir), status);
	if (status == 200) command->mdirc->subscribed = false;
	command_del(command);
}

static void rename_temp(char const *path, char const *compl)
{
	char new_path[PATH_MAX];
	int len = snprintf(new_path, sizeof(new_path), "%s", path);
	assert(len > 0 && len < (int)sizeof(new_path));
	char *c = new_path + len;
	while (*(c-1) != '+' && *(c-1) != '-') {
		if (c == new_path || *c == '/') with_error(0, "Bad filename for command : '%s'", path) return;
		c--;
	}
	snprintf(c, sizeof(new_path)-(c - new_path), "=%s", compl);
	if (0 != rename(path, new_path)) error_push(errno, "rename %s -> %s", path, new_path);
}

void finalize_put(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	int status = cmd->args[0].integer;
	char const *compl = cmd->args[1].string;
	debug("put %s : %d", command->filename, status);
	if (status == 200) {
		rename_temp(command->filename, compl);
	}
	command_del(command);
}

void finalize_rem(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	int status = cmd->args[0].integer;
	char const *compl = cmd->args[1].string;
	debug("rem %s : %d", command->filename, status);
	if (status == 200) {
		rename_temp(command->filename, compl);
	}
	command_del(command);
}

void finalize_auth(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	int status = cmd->args[0].integer;
	debug("auth : %d", status);
	if (status != 200) {
		terminate_reader = true;
	}
	command_del(command);
}

void finalize_quit(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	int status = cmd->args[0].integer;
	debug("quit : %d", status);
	terminate_reader = true;
	command_del(command);
}

struct patch {
	LIST_ENTRY(patch) entry;
	mdir_version old_version, new_version;
	struct header *header;
	enum mdir_action action;
};

static void patch_ctor(struct patch *patch, struct mdirc *mdirc, mdir_version old_version, mdir_version new_version, enum mdir_action action)
{
	patch->old_version = old_version;
	patch->new_version = new_version;
	patch->action = action;
	patch->header = header_new();
	on_error return;
	header_read(patch->header, cnx.fd);
	on_error {
		header_del(patch->header);
		return;
	}
	// Insert this patch into this mdir
	struct patch *p, *prev_p = NULL;
	LIST_FOREACH(p, &mdirc->patches, entry) {
		if (p->old_version > old_version) break;
		prev_p = p;
	}
	if (! prev_p) {
		LIST_INSERT_HEAD(&mdirc->patches, patch, entry);
	} else {
		LIST_INSERT_AFTER(prev_p, patch, entry);
	}
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

static void try_apply(struct mdirc *mdirc)	// try to apply some of the stored patches
{
	debug("try to apply received patch(es)");
	struct patch *patch;
	while (NULL != (patch = LIST_FIRST(&mdirc->patches)) && mdir_last_version(&mdirc->mdir) == patch->old_version) {
		(void)mdir_patch(&mdirc->mdir, patch->action, patch->header);
		on_error break;
		patch_del(patch);
	}
}

static struct patch *patch_new(struct mdirc *mdirc, mdir_version old_version, mdir_version new_version, enum mdir_action action)
{
	debug("fetching patch for %"PRIversion"->%"PRIversion" of '%s'", old_version, new_version, mdir_id(&mdirc->mdir));
	struct patch *patch = malloc(sizeof(*patch));
	if (! patch) with_error(ENOMEM, "malloc patch") return NULL;
	patch_ctor(patch, mdirc, old_version, new_version, action);
	on_error {
		free(patch);
		return NULL;
	}
	return patch;
}

void patch_service(struct mdir_cmd *cmd, void *user_data)
{
	assert(user_data == &cnx);
	struct mdir *const mdir = mdir_lookup_by_id(cmd->args[0].string, false);
	on_error return;
	struct mdirc *const mdirc = mdir2mdirc(mdir);
	(void)patch_new(mdirc, cmd->args[1].integer, cmd->args[2].integer, mdir_str2action(cmd->args[3].string));
	on_error return;
	try_apply(mdirc);
}

void *reader_thread(void *args)
{
	(void)args;
	debug("starting reader thread");
	terminate_reader = false;
	do {
		// read and parse one command
		debug("Reading cmd on socket");
		if_fail (mdir_cnx_read(&cnx)) break;
	} while (! terminate_reader);
	debug("Quitting reader thread");
	return NULL;
}

void reader_begin(void)
{
}

void reader_end(void)
{
}

