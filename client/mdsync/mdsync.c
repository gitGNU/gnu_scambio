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
/*
 * Fsync is a simple stdin/out client to synchronize a local directory with
 * a remote mdir folder. It runs plugins as required.
 * CLI parameters : boolean to uploads only.
 * HMI will be added later on, on top of this program.
 */
#include <stdlib.h>
#include <stdio.h>
#include <pth.h>
#include "scambio.h"
#include "daemon.h"
#include "mdir.h"

static int init_conf(void)
{
	int err;
	if (0 != (err = conf_set_default_str("MDSYNC_LOG_DIR", "/var/log"))) return err;
	if (0 != (err = conf_set_default_int("MDSYNC_LOG_LEVEL", 3))) return err;
	return 0;
}

static int init_log(void)
{
	int err;
	if (0 != (err = log_begin(conf_get_str("MDSYNC_LOG_DIR"), "fsync.log"))) return err;
	debug("init log");
	if (0 != atexit(log_end)) return -1;
	log_level = conf_get_int("MDSYNC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
	return 0;
}

static int init(void)
{
	int err;
	if (0 != (err = init_conf())) return err;
	if (0 != (err = init_log())) return err;
	if (0 != (err = daemonize())) return err;
	if (0 != (err = client_begin())) return err;
	if (0 != atexit(client_end)) return -1;
	return 0;
}



int main(int nb_args, char **args)
{
	(void)nb_args;
	(void)args;
	int ret = EXIT_FAILURE;
	if (! pth_init()) return EXIT_FAILURE;
	if (0 != init()) goto q0;
	// TODO
	ret = EXIT_SUCCESS;
q0:
	pth_kill();
	return ret;
}

