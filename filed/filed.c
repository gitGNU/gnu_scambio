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
#include <pth.h>
#include "scambio.h"
#include "scambio/cmd.h"
#include "scambio/cnx.h"
#include "scambio/channel.h"
#include "daemon.h"
#include "server.h"

/*
 * Data Definitions
 */

struct server server;
static sig_atomic_t terminate = 0;

/*
 * Init
 */

static void init_server(void)
{
	server_ctor(&server, conf_get_int("SC_FILED_PORT"));
}

static void deinit_syntax(void)
{
	server_dtor(&server);
}

static void init_conf(void)
{
	conf_set_default_str("SC_FILED_LOG_DIR", "/var/log");
	conf_set_default_int("SC_LOG_LEVEL", 3);
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
}

static void init_log(void)
{
	if_fail (log_begin(conf_get_str("SC_FILED_LOG_DIR"), "filed.log")) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init(void)
{
	if (! pth_init()) exit(EXIT_FAILURE);
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail (init_conf()) return;
	if_fail (init_log()) return;
	if_fail (daemonize("sc_filed")) return;
	if_fail (chn_begin(true)) return;
	if (0 != atexit(chn_end)) return;
	if_fail (init_server()) return;
	if (0 != atexit(deinit_syntax)) with_error(0, "atexit") return;
}

/*
 * Main
 */

int main(void)
{
	if_fail (init()) return EXIT_FAILURE;
	// Run server
	while (! terminate) {
		int fd = server_accept(&server);
		if (fd < 0) {
			error("Cannot accept connection on fd %d", server.sock_fd);
			continue;
		}
		(void)chn_cnx_new_inbound(fd);	// will be destroyed by internal reader thread
		error_clear();
	}
	pth_exit(EXIT_SUCCESS);
}
