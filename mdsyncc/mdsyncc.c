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
/* Based on the new libmdir facilities, mdirc can use the listing functions
 * instead of spiding the directory tree itself (more robust if this tree structure change
 * in the future). For instance, it can loop around mdir_patch_list(NEW)
 * to send patch submissions and subscriptions. It uses the received mdir's id to find its
 * command list (could be as well attached right into the mdir structure (like listeners are)).
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <pth.h>
#include "scambio/queue.h"
#include "scambio.h"
#include "daemon.h"
#include "scambio/cmd.h"
#include "misc.h"
#include "mdsyncc.h"
#include "command.h"
#include "auth.h"
#include "c2l.h"

/*
 * mdirc allocator
 */

extern inline struct mdirc *mdir2mdirc(struct mdir *mdir);

static struct mdir *mdirc_alloc(void)
{
	struct mdirc *mdirc = malloc(sizeof(*mdirc));
	if (! mdirc) with_error(ENOMEM, "malloc mdirc") return NULL;
	mdirc->subscribed = false;
	mdirc->nb_pending_acks = 0;
	LIST_INIT(&mdirc->commands);
	LIST_INIT(&mdirc->patches);
	LIST_INIT(&mdirc->c2l_maps);
	(void)pth_rwlock_init(&mdirc->command_lock);
	return &mdirc->mdir;
}

static void mdirc_free(struct mdir *mdir)
{
	struct mdirc *mdirc = mdir2mdirc(mdir);
	struct command *cmd;
	while (NULL != (cmd = LIST_FIRST(&mdirc->commands))) {
		command_del(cmd);
	}
	struct c2l_map *c2l;
	while (NULL != (c2l = LIST_FIRST(&mdirc->c2l_maps))) {
		c2l_del(c2l);
	}
	free(mdirc);
}

/*
 * Init
 */

/* Initialisation function init all these shared datas, then call other modules
 * init function, then spawn the connecter threads, which will spawn the reader
 * and the writer once the connection is ready.
 */

static void client_end(void)
{
	writer_end();
	reader_end();
	connecter_end();
	auth_end();
	mdir_end();
}

static void client_begin(void)
{
	debug("init client lib");
	mdir_begin();
	on_error return;
	mdir_alloc = mdirc_alloc;
	mdir_free = mdirc_free;
	conf_set_default_str("MDIRD_HOST", "127.0.0.1");
	conf_set_default_str("MDIRD_PORT", TOSTR(DEFAULT_MDIRD_PORT));
	on_error goto q0;
	if_fail (writer_begin()) goto q0;
	if_fail (reader_begin()) goto q1;
	if_fail (connecter_begin()) goto q2;
	if_fail (auth_begin()) goto q3;
	return;
q3:
	connecter_end();
q2:
	reader_end();
q1:
	writer_end();
q0:
	mdir_end();
}
static void init_conf(void)
{
	conf_set_default_str("SC_LOG_DIR", "/var/log/scambio");
	conf_set_default_int("SC_LOG_LEVEL", 3);
}

static void init_log(void)
{
	log_begin(conf_get_str("SC_LOG_DIR"), "mdsyncc.log");
	on_error return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init(void)
{
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail(init_conf()) return;
	if_fail(init_log()) return;
	if_fail(daemonize("sc_mdirc")) return;
	if_fail(client_begin()) return;
	if (0 != atexit(client_end)) with_error(0, "atexit") return;
}

int main(int nb_args, char **args)
{
	(void)nb_args;
	(void)args;
	int ret = EXIT_FAILURE;
	if (! pth_init()) return EXIT_FAILURE;
	if_fail(init()) goto q0;
	while (1) {
		connecter_thread("Alice");
		pth_sleep(10);
	}
	ret = EXIT_SUCCESS;
q0:
	pth_kill();
	return ret;
}

