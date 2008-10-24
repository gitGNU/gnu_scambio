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
#include "daemon.h"
#include "server.h"
#include "stream.h"
#include "filed.h"

/*
 * Data Definitions
 */

struct mdir_syntax syntax;
struct server server;
static sig_atomic_t terminate = 0;

/*
 * Init
 */

static void init_server(void)
{
	if_fail (mdir_syntax_ctor(&syntax)) return;
	static struct mdir_cmd_def defs[] = {
		{
			.keyword = kw_creat, .cb = serve_creat,     .nb_arg_min = 0, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_write, .cb = serve_write,     .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_read,  .cb = NULL,            .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_quit,  .cb = NULL,            .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {},
		},
		CHN_COMMON_DEFS,
	};
	for (unsigned d=0; d<sizeof_array(defs); d++) {
		if_fail (mdir_syntax_register(&syntax, defs+d)) return;
	}
	server_ctor(&server, conf_get_int("SCAMBIO_FILED_PORT"));
}

static void deinit_syntax(void)
{
	mdir_syntax_dtor(&syntax);
	server_dtor(&server);
}

static void init_conf(void)
{
	conf_set_default_str("SCAMBIO_FILED_LOG_DIR", "/var/log");
	conf_set_default_int("SCAMBIO_FILED_LOG_LEVEL", 3);
	conf_set_default_int("SCAMBIO_FILED_PORT", 21436);
}

static void init_log(void)
{
	if_fail (log_begin(conf_get_str("SCAMBIO_FILED_LOG_DIR"), "filed.log")) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("MDIRD_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init(void)
{
	if (! pth_init()) with_error(0, "pth_init") return;
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail (init_conf()) return;
	if_fail (init_log()) return;
	if_fail (daemonize()) return;
	if_fail (stream_begin()) return;
	if (0 != atexit(stream_end)) return;
	if_fail (init_server()) return;
	if (0 != atexit(deinit_syntax)) with_error(0, "atexit") return;
}

/*
 * Single connection service
 */

static void cnx_env_ctor(struct cnx_env *env, int fd)
{
	if_fail (chn_cnx_ctor_inbound(&env->cnx, &syntax, fd, incoming)) return;
	env->quit = false;
}

static struct cnx_env *cnx_env_new(int fd)
{
	struct cnx_env *env = malloc(sizeof(*env));
	if (! env) with_error(ENOMEM, "Cannot alloc a cnx_env") return NULL;
	if_fail (cnx_env_ctor(env, fd)) {
		free(env);
		env = NULL;
	}
	return env;
}

static void cnx_env_dtor(struct cnx_env *env)
{
	debug("destruct chn_cnx");
	chn_cnx_dtor(&env->cnx);
}

static void cnx_env_del(void *env_)
{
	struct cnx_env *env = env_;
	cnx_env_dtor(env);
	free(env);
}

static void *serve_cnx(void *arg)
{
	debug("New connection");
	struct cnx_env *env = arg;
	if (! pth_cleanup_push(cnx_env_del, env)) with_error(0, "Cannot add cleanup function") {
		cnx_env_del(env);
		return NULL;
	}
	do {
		pth_yield(NULL);
		/* The channel reader thread have callbacks that will receive incomming
		 * datas and write it to the stream it's associated to (ie will perform
		 * physical writes onto other TXs and/or file :
		 *
		 * - The service for the read command (download) will just lookup/create
		 * the corresponding stream, and associate it's output to the TX. It does
		 * not answer the query.
		 * 
		 * - The service for the write command (upload) will do the same, but
		 * associate this TX to this stream's input. Doesn't answer neither.
		 *
		 * - The services for copy/skip will send the data to the stream, up to
		 * their associated TXs/file.
		 *
		 * - The service for miss is handled by the chn_retransmit() facility.
		 *
		 * When a stream is created for a file, a new thread is created that will
		 * read it and write data to all it's associated reading TX, untill there
		 * is no more and the thread is killed and the stream closed.  This is
		 * not the stream but the sending TX that are responsible for keeping
		 * copies (actualy just references) of the sent data boxes for
		 * retransmissions, because several TX may have different needs in this
		 * respect (different QoS, different locations on the file, etc...). And
		 * it's simplier.
		 */
		if_fail (mdir_cnx_read(&env->cnx.cnx)) break;
	} while (! env->quit);	// FIXME: or if the reader quits for some reason
	error_clear();
	return NULL;
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
		struct cnx_env *env = cnx_env_new(fd);	// will be destroyed by sub-thread
		unless_error (void)pth_spawn(PTH_ATTR_DEFAULT, serve_cnx, env);
	}
	pth_exit(EXIT_SUCCESS);
}
