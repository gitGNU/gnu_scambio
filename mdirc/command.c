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
#include "cmd.h"
#include "mdirc.h"
#include "command.h"

/*
 * Data Definitions
 */

// NOTE: respect enum command_type order ! (FIXME with something like .0:{...}, .1:{...}, ... ?)
struct command_types command_types[NB_CMD_TYPES] = {
	{ .keyword = "sub",   .finalize = finalize_sub },
	{ .keyword = "unsub", .finalize = finalize_unsub },
	{ .keyword = "put",   .finalize = finalize_put },
	{ .keyword = "rem",   .finalize = finalize_rem },
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
	if (folder[0] == '\0') folder = "/";
	snprintf(cmd->filename, sizeof(cmd->filename), "%s", filename);
	static long long seqnum = 1;
	cmd->seqnum = seqnum++;
	cmd->creation = time(NULL);
	debug("folder = '%s', mdir id = '%s', seqnum = %lld", folder, mdir_id(&mdirc->mdir), cmd->seqnum);
	char buf[SEQ_BUF_LEN];
	Write_strs(cnx.sock_fd, cmd_seq2str(buf, cmd->seqnum), " ", command_types[type].keyword, " ", folder, NULL);
	if (type == SUB_CMD_TYPE) {
		Write_strs(cnx.sock_fd, " ", mdir_version2str(mdir_last_version(&mdirc->mdir)), NULL);
	}
	Write(cnx.sock_fd, "\n", 1);
	on_error return;
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
		if (cmd->seqnum == seqnum) return cmd;
	}
	warning("No command was sent with seqnum %lld", seqnum);
	return NULL;
}

