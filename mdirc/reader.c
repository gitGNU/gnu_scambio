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

void finalize_sub(struct command *cmd, int status, char const *compl)
{
	debug("subscribing to %s : %d %s", mdir_id(&cmd->mdirc->mdir), status, compl);
	cmd->mdirc->subscribed = true;
}

void finalize_unsub(struct command *cmd, int status, char const *compl)
{
	debug("unsubscribing to %s : %d %s", mdir_id(&cmd->mdirc->mdir), status, compl);
	cmd->mdirc->subscribed = false;
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

void finalize_put(struct command *cmd, int status, char const *compl)
{
	debug("put %s : %d", cmd->filename, status);
	if (status == 200) {
		rename_temp(cmd->filename, compl);
	}
}

void finalize_rem(struct command *cmd, int status, char const *compl)
{
	debug("rem %s : %d", cmd->filename, status);
	if (status == 200) {
		rename_temp(cmd->filename, compl);
	}
}

void finalize_auth(struct command *cmd, int status, char const *compl)
{
	(void)cmd;
	(void)compl;
	debug("auth : %d", status);
	if (status != 200) {
		terminate_reader = true;
	}
}

void finalize_quit(struct command *cmd, int status, char const *compl)
{
	(void)cmd;
	(void)status;
	(void)compl;
	debug("quit : %d", status);
	terminate_reader = true;
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
	header_read(patch->header, cnx.sock_fd);
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

static char const *const kw_patch = "patch";
static char const *const kw_quit  = "quit";

void *reader_thread(void *args)
{
	(void)args;
	debug("starting reader thread");
	terminate_reader = false;
	do {
		// read and parse one command
		struct cmd cmd;
		debug("Reading cmd on socket");
		cmd_read(&parser, &cmd, cnx.sock_fd);
		on_error break;
		debug("Received keyword '%s'", cmd.keyword);
		if (cmd.keyword == kw_patch) {
			struct mdir *const mdir = mdir_lookup_by_id(cmd.args[0].val.string, false);
			on_error break;
			struct mdirc *const mdirc = mdir2mdirc(mdir);
			(void)patch_new(mdirc, cmd.args[1].val.integer, cmd.args[2].val.integer, mdir_str2action(cmd.args[3].val.string));
			on_error break;
			try_apply(mdirc);
			on_error break;
		} else for (unsigned t=0; t<sizeof_array(command_types); t++) {
			if (cmd.keyword == command_types[t].keyword) {
				struct command *command = command_get_by_seqnum(t, cmd.seq);
				if (command) {
					command_types[t].finalize(command, cmd.args[0].val.integer, cmd.args[1].val.string);
					command_del(command);
				}
				break;
			}
		}
		cmd_dtor(&cmd);
	} while (! terminate_reader);
	debug("Quitting reader thread");
	return NULL;
}

void reader_begin(void)
{
	// TODO: register all queries (for answer) and command PATCH
	// FIXME: LIST_INIT should go in a command_begin()
	for (unsigned t=0; t<sizeof_array(command_types); t++) {
		cmd_register_keyword(&parser, command_types[t].keyword,  2, UINT_MAX, CMD_INTEGER, CMD_STRING, CMD_EOA);
		LIST_INIT(&command_types[t].commands);
	}
	cmd_register_keyword(&parser, kw_quit, 1, 1, CMD_INTEGER, CMD_EOA);
	cmd_register_keyword(&parser, kw_patch, 4, 4, CMD_STRING, CMD_INTEGER, CMD_INTEGER, CMD_STRING, CMD_EOA);
}

void reader_end(void)
{
	for (unsigned t=0; t<sizeof_array(command_types); t++) {
		cmd_unregister_keyword(&parser, command_types[t].keyword);
	}
	cmd_unregister_keyword(&parser, kw_quit);
	cmd_unregister_keyword(&parser, kw_patch);
}

