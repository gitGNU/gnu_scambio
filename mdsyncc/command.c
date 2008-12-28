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
#include "mdsyncc.h"
#include "command.h"

/*
 * Public Functions
 */

static void command_ctor(struct command *cmd, char const *kw, struct mdirc *mdirc, char const *folder, char const *filename, struct header *h)
{
	if (folder[0] == '\0') folder = "/";	// should not happen
	snprintf(cmd->filename, sizeof(cmd->filename), "%s", filename);
	cmd->mdirc = mdirc;
	cmd->kw = kw;
	debug("cmd @%p, folder = '%s', mdir id = '%s'", cmd, folder, mdir_id(&mdirc->mdir));
	if_fail (mdir_cnx_query(&cnx, kw, h, &cmd->sq, folder, kw == kw_sub ? mdir_version2str(mdir_last_version(&mdirc->mdir)) : NULL, NULL)) return;
	LIST_INSERT_HEAD(&mdirc->commands, cmd, mdirc_entry);
}

struct command *command_new(char const *kw, struct mdirc *mdirc, char const *folder, char const *filename, struct header *h)
{
	struct command *cmd = Malloc(sizeof(*cmd));
	if_fail (command_ctor(cmd, kw, mdirc, folder, filename, h)) {
		free(cmd);
		cmd = NULL;
	}
	return cmd;
}

void command_del(struct command *cmd)
{
	LIST_REMOVE(cmd, mdirc_entry);
	free(cmd);
}

#if 0
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
#endif
struct command *command_get_by_path(struct mdirc *mdirc, char const *kw, char const *path)
{
	struct command *cmd;
	LIST_FOREACH(cmd, &mdirc->commands, mdirc_entry) {
		debug("lookup cmd @%p, path = '%s'", cmd, cmd->filename);
		if (cmd->kw == kw && 0 == strcmp(cmd->filename, path)) return cmd;
	}
	debug("No command was sent for this path (%s) and keyword (%s)", path, kw);
	return NULL;
}

