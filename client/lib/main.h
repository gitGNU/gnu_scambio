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
#ifndef MAIN_H_080731
#define MAIN_H_080731

#include <limits.h>
#include <stdbool.h>
#include <pth.h>
#include "cnx.h"
#include "queue.h"
#include "mdir.h"

extern struct cnx_client cnx;
char root_dir[PATH_MAX];	// without trailing '/'
size_t root_dir_len;
typedef void *thread_entry(void *);
thread_entry connecter_thread, reader_thread, writer_thread;
pth_t reader_pthid, writer_pthid;

int reader_begin(void);
void reader_end(void);
int writer_begin(void);
void writer_end(void);
struct command;
int finalize_sub(struct command *cmd, int status);
int finalize_unsub(struct command *cmd, int status);
int finalize_put(struct command *cmd, int status);
int finalize_rem(struct command *cmd, int status);
int finalize_class(struct command *cmd, int status);
int finalize_quit(struct command *cmd, int status);

struct run_path {
	char root[PATH_MAX];
	TAILQ_ENTRY(run_path) entry;
};
void run_path_del(struct run_path *rp);
int run_path_new(struct run_path **rp);	// return an empty run path
struct run_path *shift_run_queue(void);
void push_run_queue(struct run_path *rp);

extern LIST_HEAD(commands, command) pending_subs, pending_unsub, subscribed;
extern pth_rwlock_t subscriptions_lock;
struct commands pending_puts, pending_rems;
pth_rwlock_t pending_puts_lock, pending_rems_lock;

struct command {
	LIST_ENTRY(command) entry;
	char folder[PATH_MAX];	// folder's path (absolute)
	char path[PATH_MAX];	// the involved, discriminent, absolute filename
	long long seqnum;	// irrelevant if SUBSCRIBED
	time_t creation;	// idem
};
enum command_type {
	SUB_CMD_TYPE, UNSUB_CMD_TYPE, PUT_CMD_TYPE, REM_CMD_TYPE, CLASS_CMD_TYPE, QUIT_CMD_TYPE, NB_COMMAND_TYPES
};
extern struct command_types {
	char const *const keyword;
	struct commands list;
	int (*finalize)(struct command *cmd, int status);
} command_types[NB_COMMAND_TYPES];
pth_rwlock_t command_types_lock;	// protect all previous array

// give absolute filename/path
int command_new(struct command **cmd, enum command_type type, char const *folder, char const *filename);
void command_del(struct command *cmd);
int command_get_by_path(struct command **cmd, struct commands *list, char const *path, bool do_timeout);
int command_get_by_seqnum(struct command **cmd, struct commands *list, long long seqnum);
bool command_timeouted(struct command *cmd);
void command_touch_renew_seqnum(struct command *cmd);
void command_change_list(struct command *cmd, struct commands *list);

#endif
