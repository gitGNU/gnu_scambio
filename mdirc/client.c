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
#include "client.h"
#include "misc.h"
#include "cmd.h"

/* Those two threads share some structures related to the shared socket :
 * First the socket itself
 */

struct cnx_client cnx;

/* Then, lists of pending commands, starting with subscriptions.
 *
 * The writer will add entries to these lists while the reader will remove them once completed,
 * and perform the proper final action for these commands.
 */

#include <limits.h>
#include <time.h>
#include "queue.h"
// NOTE: respect enum command_type order ! (FIXME with something like .0:{...}, .1:{...}, ... ?)
struct command_types command_types[NB_COMMAND_TYPES] = {
	{ .keyword = "sub",   .finalize = finalize_sub },
	{ .keyword = "unsub", .finalize = finalize_unsub },
	{ .keyword = "put",   .finalize = finalize_put },
	{ .keyword = "rem",   .finalize = finalize_rem },
	{ .keyword = "quit",  .finalize = finalize_quit},
};
struct commands subscribed;
pth_rwlock_t command_types_lock;

#include <time.h>
bool command_timeouted(struct command *cmd)
{
#	define CMD_TIMEOUT 15
	return time(NULL) - cmd->creation > CMD_TIMEOUT;
}

void command_touch_renew_seqnum(struct command *cmd)
{
	static long long seqnum = 0;
	cmd->seqnum = seqnum++;
	cmd->creation = time(NULL);
}

static void command_ctor(struct command *cmd, enum command_type type, char const *folder, char const *path)
{
	if (folder[0] == '\0') folder = "/";
	char buf[SEQ_BUF_LEN];
	snprintf(cmd->folder, sizeof(cmd->folder), "%s", folder);
	snprintf(cmd->path,   sizeof(cmd->path),   "%s", path);
	command_touch_renew_seqnum(cmd);
	debug("folder = '%s', mdir_root = '%s' (len=%zu)", folder, mdir_root, mdir_root_len);
	Write_strs(cnx.sock_fd, cmd_seq2str(buf, cmd->seqnum), " ", command_types[type].keyword, " ", folder, "\n", NULL);
	unless_error LIST_INSERT_HEAD(&command_types[type].list, cmd, entry);
}

struct command *command_new(enum command_type type, char const *folder, char const *path)
{
	struct command *cmd = malloc(sizeof(*cmd));
	if (! *cmd) with_error(ENOMEM, "malloc cmd") return NULL;
	command_ctor(cmd, type, folder, path);
	on_error {
		free(cmd);
		cmd = NULL;
	}
	return cmd;
}

void command_del(struct command *cmd)
{
	LIST_REMOVE(cmd, entry);
	free(cmd);
}

void command_change_list(struct command *cmd, struct commands *list)
{
	LIST_REMOVE(cmd, entry);
	LIST_INSERT_HEAD(list, cmd, entry);
}

#include <string.h>
struct command *command_get_by_path(struct commands *list, char const *path, bool do_timeout)
{
	struct command *tmp;
	LIST_FOREACH_SAFE(cmd, list, entry, tmp) {
		if (0 == strcmp(path, cmd->path)) {
			if (! do_timeout) return cmp;
			if (! command_timeouted(cmd)) with_error(EINPROGRESS, "Not timeouted") return cmd;
			command_del(cmd);
		}
	}
	warning("No command was sent with path %s", path);
	return NULL;
}

struct command *command_get_by_seqnum(struct commands *list, long long seqnum)
{
	LIST_FOREACH(cmd, list, entry) {
		if (cmd->seqnum == seqnum) return cmd;
	}
	warning("No command was sent with seqnum %lld", seqnum);
	return NULL;
}

/* Then PUT/REM commands.
 * Same notes as above.
 */

/* Initialisation function init all these shared datas, then call other modules
 * init function, then spawn the connecter threads, which will spawn the reader
 * and the writer once the connection is ready.
 */
#include <stdlib.h>
#include "conf.h"
void client_end(void)
{
	writer_end();
	reader_end();
	mdir_end();
	cmd_end();
}

void client_begin(void)
{
	debug("init client lib");
	cmd_begin();
	on_error return;
	mdir_begin();
	on_error goto q0;
	conf_set_default_str("MDIRD_HOST", "127.0.0.1");
	conf_set_default_str("MDIRD_PORT", TOSTR(DEFAULT_MDIRD_PORT));
	on_error goto q1;
	for (unsigned t=0; t<sizeof_array(command_types); t++) {
		LIST_INIT(&command_types[t].list);
	}
	(void)pth_rwlock_init(&command_types_lock);
	LIST_INIT(&subscribed);
	writer_begin();
	on_error goto q1;
	reader_begin();
	on_error goto q2;
	return;
q2:
	writer_end();
q1:
	mdir_end();
q0:
	cmd_end();
}

