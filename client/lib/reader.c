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
#include "main.h"
#include "scambio.h"
#include "cmd.h"

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
				// read header, then command
				struct header *header = header_new(msg); // FIXME: il faudrait un header_new_fd() ?
				
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
	cmd_register_keyword(kw_patch, 1, 2, CMD_STRING, CMD_EOA);
	return 0;
}

void reader_end(void)
{
	for (unsigned t=0; t<sizeof_array(command_types); t++) {
		cmd_unregister_keyword(command_types[t].keyword);
	}
	cmd_unregister_keyword(kw_patch);
}

