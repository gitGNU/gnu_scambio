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
#include "cnx.h"
#include "queue.h"

extern struct cnx_client cnx;
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

#include <pth.h>
extern pth_rwlock_t subscriptions_lock;
static inline void subscription_lock(void)
{
	(void)pth_rwlock_acquire(&subscriptions_lock, PTH_RWLOCK_RW, FALSE, NULL);
}
static inline void subscription_unlock(void)
{
	(void)pth_rwlock_release(&subscriptions_lock);
}
struct command;
int subscription_new(struct command **cmd, char const *path);
void command_del(struct command *cmd);
struct command *subscription_get(char const *path);
struct command *subscription_get_pending(char const *path);

#endif
