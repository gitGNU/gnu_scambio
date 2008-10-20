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
/* The client first connect to a mdird, then spawns two threads : one that writes and
 * one that reads the socket. Since connection can takes some time, this whole process is
 * done on the connector thread, thus execution flow is returned at once to the user.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pth.h>
#include "scambio.h"
#include "misc.h"
#include "scambio/cmd.h"
#include "mdirc.h"
#include "command.h"

/*
 * Data Definitions
 */

// NOTE: respect enum command_type order ! (FIXME with something like .0:{...}, .1:{...}, ... ?)
struct command_types command_types[NB_CMD_TYPES] = {
//	{ .keyword = kw_auth,  .finalize = finalize_auth },
	{ .keyword = kw_sub,   .finalize = finalize_sub },
	{ .keyword = kw_unsub, .finalize = finalize_unsub },
	{ .keyword = kw_put,   .finalize = finalize_put },
	{ .keyword = kw_rem,   .finalize = finalize_rem },
	{ .keyword = kw_quit,  .finalize = finalize_quit },
};

/*
 * Public Functions
 */

#include <time.h>
bool command_timeouted(struct command *cmd)
{
#	define CMD_TIMEOUT 15
	return time(NULL) - cmd->creation > CMD_TIMEOUT;
}

static void command_ctor(struct command *cmd, enum command_type type, struct mdirc *mdirc, char const *folder, char const *filename)
{
	assert(type < NB_CMD_TYPES);
	if (folder[0] == '\0') folder = "/";	// should not happen
	snprintf(cmd->filename, sizeof(cmd->filename), "%s", filename);
	cmd->mdirc = mdirc;
	cmd->creation = time(NULL);	// FIXME: timeout of queries should go into mdir_cnx_read()
	debug("cmd @%p, folder = '%s', mdir id = '%s', seqnum = %lld", cmd, folder, mdir_id(&mdirc->mdir), cmd->seqnum);
	if_fail (mdir_cnx_query(&cnx, &command_types[type].def, true, cmd, folder, type == SUB_CMD_TYPE ? mdir_version2str(mdir_last_version(&mdirc->mdir)) : NULL, NULL)) return;
	LIST_INSERT_HEAD(mdirc->commands+type, cmd, mdirc_entry);
	LIST_INSERT_HEAD(&command_types[type].commands, cmd, type_entry);
}

struct command *command_new(enum command_type type, struct mdirc *mdirc, char const *folder, char const *filename)
{
	struct command *cmd = malloc(sizeof(*cmd));
	if (! cmd) with_error(ENOMEM, "malloc cmd") return NULL;
	command_ctor(cmd, type, mdirc, folder, filename);
	on_error {
		free(cmd);
		cmd = NULL;
	}
	return cmd;
}

void command_del(struct command *cmd)
{
	LIST_REMOVE(cmd, mdirc_entry);
	LIST_REMOVE(cmd, type_entry);
	free(cmd);
}

struct command *command_get_by_seqnum(unsigned type, long long seqnum)
{
	assert(type < sizeof_array(command_types));
	struct command *cmd;
	LIST_FOREACH(cmd, &command_types[type].commands, type_entry) {
		debug("lookup cmd @%p, seqnum = %lld", cmd, cmd->seqnum);
		if (cmd->seqnum == seqnum) return cmd;
	}
	warning("No command was sent with seqnum %lld", seqnum);
	return NULL;
}

struct command *command_get_by_path(struct mdirc *mdirc, unsigned type, char const *path)
{
	assert(type < sizeof_array(command_types));
	struct command *cmd;
	LIST_FOREACH(cmd, &mdirc->commands[type], mdirc_entry) {
		debug("lookup cmd @%p, path = '%s'", cmd, cmd->filename);
		if (0 == strcmp(cmd->filename, path)) return cmd;
	}
	debug("No command was sent for this path (%s)", path);
	return NULL;
}

