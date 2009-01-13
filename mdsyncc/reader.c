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
#include "mdsyncc.h"
#include "scambio.h"
#include "scambio/cnx.h"
#include "scambio/header.h"
#include "digest.h"
#include "c2l.h"

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
	if (status == 200) {
		command->mdirc->subscribed = true;
	} else {
		info("Cannot subscribe to '%s'", mdir_id(&command->mdirc->mdir));
		command->mdirc->quarantine = time(NULL) + 5*60;
	}
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

static void remove_local_file(char const *local_file)
{
	debug("Removing local file '%s'", local_file);
	if (0 != unlink(local_file)) with_error(errno, "unlink(%s)", local_file) return;
}

static void remember_version_map(struct mdirc *mdirc, char const *central_str, char const *local_file)
{
	// Stores the mapping between central and local version
	mdir_version central, local;
	if_fail (central = mdir_str2version(central_str)) return;
	char const *c = local_file + strlen(local_file);
	while (c > local_file && *(c-1) != '/') c--;
	if (c == local_file) with_error(0, "Bad filename : %s", local_file) return;
	if_fail (local = mdir_str2version(c+1)) return;
	(void)c2l_new(&mdirc->c2l_maps, central, local);
}

static bool retry_later(int status)
{
	return status >= 300 && status < 400;
}

static void fin_putrem(struct command *command, int status, char const *compl)
{
	assert(command->filename[0] != '\0');
	if (status == 200) {
		remember_version_map(command->mdirc, compl, command->filename);
		remove_local_file(command->filename);
	} else if (! retry_later(status)) {
		remove_local_file(command->filename);
		// Save the error
		char status_str[1024];
		snprintf(status_str, sizeof(status_str), "%d; error=\"%s\"", status, compl);
		(void)header_field_new(command->header, SC_STATUS_FIELD, status_str);
		char *tmp = strrchr(command->filename, '.');
		assert(tmp && tmp[1] == 't' && tmp[2] == 'm' && tmp[3] == 'p');
		tmp[1] = 'e'; tmp[2] = 'r'; tmp[3] = 'r';
		header_to_file(command->header, command->filename);
	}
	error_clear();
	command_del(command);
}

void finalize_put(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	assert(command->mdirc->nb_pending_acks > 0);
	command->mdirc->nb_pending_acks--;
	int status = cmd->args[0].integer;
	char const *compl = cmd->args[1].string;
	debug("put %s : %d", command->filename, status);
	fin_putrem(command, status, compl);
}

void finalize_rem(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct mdir_sent_query *const sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *const command = DOWNCAST(sq, sq, command);
	assert(command->mdirc->nb_pending_acks > 0);
	command->mdirc->nb_pending_acks--;
	int status = cmd->args[0].integer;
	char const *compl = cmd->args[1].string;
	debug("rem %s : %d", command->filename, status);
	fin_putrem(command, status, compl);
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
		header_unref(patch->header);
		return;
	}
	struct c2l_map *c2l = c2l_search(&mdirc->c2l_maps, new_version);
	if (c2l) {
		debug("version %"PRIversion" found to match local version %"PRIversion, new_version, c2l->local);
		(void)header_field_new(patch->header, SC_LOCALID_FIELD, mdir_version2str(c2l->local));
		c2l_del(c2l);
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
	header_unref(patch->header);
}

static void patch_del(struct patch *patch)
{
	patch_dtor(patch);
	free(patch);
}

static void try_apply(struct mdirc *mdirc)	// try to apply some of the stored patches
{
	if (mdirc->nb_pending_acks > 0) {
		// mdir_patch_list() is in trouble is we receive a PATCH for a local transient
		// file before the acks gives us its version. Here is our work arnound this :
		debug("Do not apply patches because we still wait for %u acks", mdirc->nb_pending_acks);
		return;
	}
	debug("try to apply received patch(es)");
	struct patch *patch;
	while (NULL != (patch = LIST_FIRST(&mdirc->patches)) && mdir_last_version(&mdirc->mdir) == patch->old_version) {
		assert(patch->new_version > patch->old_version);
		unsigned nb_deleted = patch->new_version - patch->old_version - 1;
		mdir_version version;
		if_fail (version = mdir_patch(&mdirc->mdir, patch->action, patch->header, nb_deleted)) break;
		assert(version == patch->new_version);
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

