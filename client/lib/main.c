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
/* The client lib first connect to a mdird, then spawns two threads : one that writes and
 * one that reads the socket. Since connection can takes some time, this whole process is
 * done on the connector thread, thus execution flow is returned at once to the user.
 */

#include <stdlib.h>
#include <pth.h>
#include <errno.h>
#include "scambio.h"
#include "main.h"

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
struct command {
	LIST_ENTRY(command) entry;
	char path[PATH_MAX];	// folder's path relative to root dir
	long long seqnum;	// irrelevant if SUBSCRIBED
	time_t creation;	// idem
};
static LIST_HEAD(commands, command) subscribing, subscribed, unsubscribing;
pth_rwlock_t subscriptions_lock;

extern inline void subscription_lock(void);
extern inline void subscription_unlock(void);

#include <time.h>
static int command_timeouted(struct command *cmd)
{
#	define CMD_TIMEOUT 15
	return time(NULL) - cmd->creation > CMD_TIMEOUT;
}

static void command_ctor(struct command *cmd, struct commands *list, char const *path)
{
	static long long seqnum = 0;
	snprintf(cmd->path, sizeof(cmd->path), "%s", path);
	cmd->seqnum = seqnum++;
	cmd->creation = time(NULL);
	LIST_INSERT_HEAD(list, cmd, entry);
}

int subscription_new(struct command **cmd, char const *path)
{
	*cmd = malloc(sizeof(**cmd));
	if (! *cmd) return -ENOMEM;
	command_ctor(*cmd, &subscribing, path);
	return 0;
}

void command_del(struct command *cmd)
{
	LIST_REMOVE(cmd, entry);
	free(cmd);
}

#include <string.h>
static struct command *command_get_from_list(struct commands *list, char const *path)
{
	struct command *cmd;
	LIST_FOREACH(cmd, list, entry) {
		if (0 == strcmp(path, cmd->path)) return cmd;
	}
	return NULL;
}
struct command *subscription_get(char const *path)
{
	return command_get_from_list(&subscribed, path);
}
struct command *subscription_get_pending(char const *path)
{
	struct command *cmd = command_get_from_list(&subscribing, path);
	if (cmd && command_timeouted(cmd)) {
		command_del(cmd);
		cmd = NULL;
	}
	return cmd;
}

/* Then PUT/REM commands.
 * Same notes as above.
 */

struct putrem_command {
	LIST_ENTRY(putrem_command) entry;
	char path[PATH_MAX];	// absolute path to the meta file
	long long seqnum;
	time_t send;
};
static LIST_HEAD(putrem_commands, putrem_command) put_commands, rem_commands;
static pth_rwlock_t put_commands_lock, rem_commands_lock;

/* The writer thread works by traversing a directory tree from a given root.
 * It dies this for every run path on its run queue.
 * When a plugin adds/removes content or change the subscription state, it adds
 * the location of this action into the root queue, as well as a flag telling if the
 * traversall should be recursive or not. For instance, adding a message means
 * adding a tempfile in the folder's .put directory and pushing this folder
 * into the root queue while removing one means linking its meta onto the .rem
 * directory (the actual meta will be deleted on reception of the suppression patch).
 * If the so called plugin is an external program instead, it can signal the client
 * programm that it should, recursively, reparse the whole tree.
 * This run queue is of course shared by several threads (writer and plugins).
 */
static TAILQ_HEAD(run_paths, run_path) run_queue;
static pth_rwlock_t run_queue_lock;

struct run_path *shift_run_queue(void)
{
	(void)pth_rwlock_acquire(&run_queue_lock, PTH_RWLOCK_RW, FALSE, NULL);
	struct run_path *rp = TAILQ_FIRST(&run_queue);
	if (! rp) return NULL;
	TAILQ_REMOVE(&run_queue, rp, entry);
	(void)pth_rwlock_release(&run_queue_lock);
	return rp;
}

void run_path_del(struct run_path *rp)
{
	free(rp);
}

/* Initialisation function init all these shared datas, then call other modules
 * init function, then spawn the connecter threads, which will spawn the reader
 * and the writer once the connection is ready.
 */
#include <stdlib.h>
#include "conf.h"
static void client_end(void)
{
	writer_end();
	reader_end();
}

int client_begin(void)
{
	int err;
	if (0 != (err = conf_set_default_str("MDIRD_SERVER", "127.0.0.1"))) return err;
	if (0 != (err = conf_set_default_str("MDIRD_PORT", TOSTR(DEFAULT_MDIRD_PORT)))) return err;
	(void)pth_rwlock_init(&subscriptions_lock);
	LIST_INIT(&subscribing);
	LIST_INIT(&subscribed);
	LIST_INIT(&unsubscribing);
	(void)pth_rwlock_init(&put_commands_lock);
	(void)pth_rwlock_init(&rem_commands_lock);
	LIST_INIT(&put_commands);
	LIST_INIT(&rem_commands);
	(void)pth_rwlock_init(&run_queue_lock);
	TAILQ_INIT(&run_queue);
	atexit(client_end);
	if (0 != (err = writer_begin())) goto q0;
	if (0 != (err = reader_begin())) goto q1;
	if (NULL == pth_spawn(PTH_ATTR_DEFAULT, connecter_thread, NULL)) {
		err = -EINVAL;
		goto q2;
	}
	return 0;
q2:
	reader_end();
q1:
	writer_end();
q0:
	return err;
}

