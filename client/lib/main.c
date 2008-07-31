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

#include <pth.h>
#include <errno.h>
#include "scambio.h"
#include "main.h"

/* Those two threads share some structures related to the shared socket :
 * First the socket itself
 */

struct cnx_client cnx;

/* Then, lists of pending commands.
 *
 * The writer will add entries to these lists while the reader will remove them once completed,
 * and perform the proper final action for these commands.
 */

#include <limits.h>
#include <time.h>
#include "queue.h"
struct subscription {
	enum subscription_state { SUBSCRIBING, SUBSCRIBED, UNSUBSCRIBING } state;
	LIST_ENTRY(sub_command) entry;	// the list depends on the state
	char rel_path[PATH_MAX];	// folder's path relative to root dir
	long long seq;	// irrelevant if SUBSCRIBED
	time_t send;	// idem
};
static LIST_HEAD(subscriptions, subscription) subcribing, subscribed, unsubscribing;
static pth_rwlock_t subscribing_lock, subscribed_lock, unsubscribing_lock;

struct putrem_command {
	LIST_ENTRY(putrem_command) entry;
	char meta_path[PATH_MAX];	// absolute path to the meta file
	long long seq;
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
struct run_path {
	char root[PATH_MAX];
	TAILQ_ENTRY(run_path) entry;
};
static TAILQ_HEAD(run_paths, run_path) run_queue;
static pth_rwlock_t run_queue_lock;

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
	(void)pth_rwlock_init(&subscribing_lock);
	(void)pth_rwlock_init(&subscribed_lock);
	(void)pth_rwlock_init(&unsubscribing_lock);
	LIST_INIT(&subcribing);
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

