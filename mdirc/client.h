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
#ifndef CLIENT_H_080731
#define CLIENT_H_080731

#include <limits.h>
#include <stdbool.h>
#include <pth.h>
#include "cnx.h"
#include "queue.h"
#include "mdir.h"

extern struct cnx_client cnx;
typedef void *thread_entry(void *);
thread_entry connecter_thread, reader_thread, writer_thread;
pth_t reader_pthid, writer_pthid;

void reader_begin(void);
void reader_end(void);
void writer_begin(void);
void writer_end(void);
struct command;
void finalize_sub(struct command *cmd, int status);
void finalize_unsub(struct command *cmd, int status);
void finalize_put(struct command *cmd, int status);
void finalize_rem(struct command *cmd, int status);
void finalize_quit(struct command *cmd, int status);

extern LIST_HEAD(commands, command) pending_subs, pending_unsub, subscribed;
extern pth_rwlock_t subscriptions_lock;
struct commands pending_puts, pending_rems;
pth_rwlock_t pending_puts_lock, pending_rems_lock;

// commands that were sent for which we wait an answer
// or successfull subscriptions (kept in a separate list)
struct command {
	LIST_ENTRY(command) entry;
	char folder[PATH_MAX];	// folder's path (relative to mdir_root)
	char path[PATH_MAX];	// the involved, discriminent, absolute filename
	long long seqnum;	// irrelevant if SUBSCRIBED
	time_t creation;	// idem
};
enum command_type {
	SUB_CMD_TYPE, UNSUB_CMD_TYPE, PUT_CMD_TYPE, REM_CMD_TYPE, QUIT_CMD_TYPE, NB_COMMAND_TYPES
};
extern struct command_types {
	char const *const keyword;
	struct commands list;
	void (*finalize)(struct command *cmd, int status);
} command_types[NB_COMMAND_TYPES];
pth_rwlock_t command_types_lock;	// protect all previous array

// give absolute filename/path
struct command *command_new(enum command_type type, char const *folder, char const *filename);
void command_del(struct command *cmd);
struct command *command_get_by_path(struct commands *list, char const *path, bool do_timeout);
struct command *command_get_by_seqnum(struct commands *list, long long seqnum);
bool command_timeouted(struct command *cmd);
void command_touch_renew_seqnum(struct command *cmd);
void command_change_list(struct command *cmd, struct commands *list);

void client_begin(void);
void client_end(void);
void push_path(char const *path);

#endif
