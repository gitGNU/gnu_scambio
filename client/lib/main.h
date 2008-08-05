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
#include <pth.h>
#include "cnx.h"
#include "queue.h"

extern struct cnx_client cnx;
char const *root_dir;
size_t root_dir_len;
typedef void *thread_entry(void *);
thread_entry connecter_thread, reader_thread, writer_thread;

int reader_begin(void);
void reader_end(void);
int writer_begin(void);
void writer_end(void);

struct run_path {
	char root[PATH_MAX];
	TAILQ_ENTRY(run_path) entry;
};
struct run_path *shift_run_queue(void);
void run_path_del(struct run_path *rp);

extern LIST_HEAD(commands, command) subscribing, subscribed, unsubscribing;
extern pth_rwlock_t subscriptions_lock;
struct commands put_commands, rem_commands;
pth_rwlock_t put_commands_lock, rem_commands_lock;

struct command {
	LIST_ENTRY(command) entry;
	char path[PATH_MAX];	// folder's path relative to root dir
	long long seqnum;	// irrelevant if SUBSCRIBED
	time_t creation;	// idem
};

int command_new(struct command **cmd, char const *path);
void command_del(struct command *cmd);
struct command *command_get(struct commands *list, char const *path);
struct command *command_get_with_timeout(struct commands *list, char const *path);
#include <stdbool.h>
bool command_timeouted(struct command *cmd);
void command_touch_renew_seqnum(struct command *cmd);
void command_change_list(struct command *cmd, struct commands *list);

#endif
