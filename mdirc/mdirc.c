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
#include <pth.h>
#include "scambio.h"
#include "daemon.h"
#include "client.h"

static void init_conf(void)
{
	conf_set_default_str("MDIRC_LOG_DIR", "/tmp");
	conf_set_default_int("MDIRC_LOG_LEVEL", 3);
}

static void init_log(void)
{
	log_begin(conf_get_str("MDIRC_LOG_DIR"), "mdirc.log");
	on_error return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("MDSYNC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init(void)
{
	init_conf();
	on_error return;
	init_log();
	on_error return;
	daemonize();
	on_error return;
	client_begin();
	on_error return;
	if (0 != atexit(client_end)) with_error(0, "atexit") return;
}

int main(int nb_args, char **args)
{
	(void)nb_args;
	(void)args;
	int ret = EXIT_FAILURE;
	if (! pth_init()) return EXIT_FAILURE;
	init();
	on_error goto q0;
	while (1) {
		connecter_thread(NULL);
	}
	ret = EXIT_SUCCESS;
q0:
	pth_kill();
	return ret;
}

