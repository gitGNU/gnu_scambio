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
/* This programms merely try to connect to the file server,
 * and attempt to send all files that are waiting in putdir.
 */
#include <stdlib.h>
#include <stdio.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/channel.h"
#include "daemon.h"
#include "options.h"

/*
 * Data Definitions
 */

static struct chn_cnx *ccnx;
static bool make_daemon = false;

/*
 * Inits
 */

static void init_conf(void)
{
	conf_set_default_str("SC_LOG_DIR", "/var/log/scambio");
	conf_set_default_int("SC_LOG_LEVEL", 3);
	conf_set_default_str("SC_FILED_HOST", "localhost");
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
	conf_set_default_str("SC_USERNAME", "Alice");
}

static void init_log(void)
{
	if_fail (log_begin(conf_get_str("SC_LOG_DIR"), "filec.log")) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static int init()
{
	if (! pth_init()) exit(EXIT_FAILURE);
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return -1;
	if_fail (init_conf()) return -1;
	if_fail (init_log()) return -1;
	return 0;
}

/*
 * Main thread
 */

static void loop(void)
{
	do {
		unsigned sent = chn_send_all(ccnx);
		if (sent > 0) {
			info("Sent %u files", sent);
		}
		if (make_daemon) pth_sleep(1);
	} while (make_daemon);
}

/*
 * Main
 */

int main(int nb_args, char const **args)
{
	if (0 != init()) return EXIT_FAILURE;
	struct option options[] = {
		{
			'd', "daemonize", OPT_FLAG, &make_daemon, "Keep running in background", {},
		},
	};
	if_fail (option_parse(nb_args, args, options, sizeof_array(options))) return EXIT_FAILURE;
	if (make_daemon) {
		if_fail (daemonize("sc_filec")) return EXIT_FAILURE;
	}
	// Further init
	if_fail (chn_begin(false)) return EXIT_FAILURE;
	if (0 != atexit(chn_end)) return EXIT_FAILURE;
	if_fail (ccnx = chn_cnx_new_outbound(conf_get_str("SC_FILED_HOST"), conf_get_str("SC_FILED_PORT"), conf_get_str("SC_USERNAME"))) return EXIT_FAILURE;
	if_fail (loop()) return EXIT_FAILURE;
	chn_cnx_del(ccnx);

	return EXIT_SUCCESS;
}
