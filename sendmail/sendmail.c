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
#include <stdlib.h>
#include <stdio.h>
#include "scambio.h"
#include "auth.h"
#include "daemon.h"
#include "sendmail.h"

/*
 * Data Definitions
 */

bool terminate = false;
struct chn_cnx ccnx;

/*
 * Init functions
 */

static void init_conf(void)
{
	conf_set_default_str("SC_LOG_DIR", "/var/log/scambio");
	conf_set_default_int("SC_LOG_LEVEL", 3);
	conf_set_default_str("SC_FILED_HOST", "localhost");
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
	conf_set_default_str("SC_USERNAME", "sendmail");
}

static void init_log(void)
{
	log_begin(conf_get_str("SC_LOG_DIR"), "sendmail");
	on_error return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void deinit_filed(void)
{
	chn_cnx_dtor(&ccnx);
}

static void init_filed(void)
{
	if_fail (auth_init()) return;
	if_fail (chn_init(false)) return;
	char const *host, *serv, *user;
	if_fail (host = conf_get_str("SC_FILED_HOST")) return;
	if_fail (serv = conf_get_str("SC_FILED_PORT")) return;
	if_fail (user = conf_get_str("SC_USERNAME")) return;
	if_fail (chn_cnx_ctor_outbound(&ccnx, host, serv, user)) return;
	if (0 != atexit(deinit_filed)) with_error(0, "atexit") return;
}

static void init(void)
{
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail (init_conf()) return;
	if_fail (init_log()) return;
	if_fail (daemonize("sc_sendmail")) return;
	if_fail (mdir_init()) return;
	if_fail (init_filed()) return;
}

/*
 * Main thread
 */

static void loop(void)
{
	while (! terminate) {
		(void)pth_sleep(60);
		debug("TODO");
	}
}

int main(void)
{
	if (! pth_init()) return EXIT_FAILURE;
	if_fail (init()) return EXIT_FAILURE;
	// Start the forwarder
	if_fail (forwarder_begin()) return EXIT_FAILURE;
	atexit(forwarder_end);
	// Start the ToSend crawler
	if_fail (crawler_begin()) return EXIT_FAILURE;
	atexit(crawler_end);
	// Now this thread merely reports some stats
	loop();
	debug("Quit");
	pth_kill();
}

