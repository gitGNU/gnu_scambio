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
#include <unistd.h>
#include <pth.h>
#include <string.h>
#include "scambio.h"
#include "daemon.h"
#include "server.h"
#include "varbuf.h"
#include "scambio/cnx.h"
#include "mdird.h"
#include "sub.h"

/*
 * Data Definitions
 */

static struct server server;
static struct mdir_syntax syntax;
static sig_atomic_t terminate = 0;

/*
 * We overload mdir into mdird
 */

static struct mdir *mdird_alloc(void)
{
	struct mdird *mdird = malloc(sizeof(*mdird));
	if (! mdird) with_error(ENOMEM, "malloc mdird") return NULL;
	LIST_INIT(&mdird->subscriptions);
	return &mdird->mdir;
}

static void mdird_free(struct mdir *mdir)
{
	struct mdird *mdird = mdir2mdird(mdir);
	struct subscription *sub;
	while (NULL != (sub = LIST_FIRST(&mdird->subscriptions))) {
		subscription_del(sub);	// will remove from list
	}
	free(mdird);
}

extern inline struct mdird *mdir2mdird(struct mdir *mdir);

/*
 * Init functions
 */

static void init_conf(void)
{
	conf_set_default_str("MDIRD_LOG_DIR", "/var/log");
	conf_set_default_int("MDIRD_LOG_LEVEL", 3);
	conf_set_default_int("MDIRD_PORT", DEFAULT_MDIRD_PORT);
}

static void init_log(void)
{
	log_begin(conf_get_str("MDIRD_LOG_DIR"), "mdird.log");
	on_error return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("MDIRD_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void deinit_server(void)
{
	server_dtor(&server);
}

static void init_syntax(void)
{
	mdir_syntax_ctor(&syntax);
	static struct mdir_cmd_def services[] = {
		{
			.keyword = kw_quit,  .cb = exec_quit,  .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0,
		}, {
			.keyword = kw_unsub, .cb = exec_unsub, .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }
		}, {
			.keyword = kw_sub,   .cb = exec_sub,   .nb_arg_min = 2, .nb_arg_max = 2,
			.nb_types = 2, .types = { CMD_STRING, CMD_STRING }
		}, {
			.keyword = kw_rem,   .cb = exec_rem,   .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }
		}, {
			.keyword = kw_put,   .cb = exec_put,   .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }
		}, {
			.keyword = kw_auth,  .cb = exec_auth,  .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }
		}
	};
	for (unsigned s=0; s<sizeof_array(services); s++) {
		if_fail (mdir_syntax_register(&syntax, services+s)) return;
	}
}

static void deinit_syntax(void)
{
	mdir_syntax_dtor(&syntax);
}

static void init_server(void)
{
	debug("init server");
	if_fail (init_syntax()) return;
	if(0 != atexit(deinit_syntax)) with_error(0, "atexit") return;
	if_fail (server_ctor(&server, conf_get_int("MDIRD_PORT"))) return;
	if (0 != atexit(deinit_server)) with_error(0, "atexit") return;
	mdir_begin();
	mdir_alloc = mdird_alloc;
	mdir_free = mdird_free;
	on_error return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
	exec_begin();
	on_error return;
	if (0 != atexit(exec_end)) with_error(0, "atexit") return;
	if_fail(auth_begin()) return;
	if (0 != atexit(auth_end)) with_error(0, "atexit") return;
}

static void init(void)
{
	if (! pth_init()) with_error(0, "pth_init") return;
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	init_conf(); on_error return;
	init_log();  on_error return;
	daemonize(); on_error return;
	init_server();
}

/*
 * Server
 */

static void cnx_env_del(void *env_)
{
	struct cnx_env *env = env_;
	struct subscription *sub;
	while (NULL != (sub = LIST_FIRST(&env->subscriptions))) {
		subscription_del(sub);
	}
	mdir_cnx_dtor(&env->cnx);
	free(env);
}

static void cnx_env_ctor(struct cnx_env *env, int fd)
{
	if_fail (mdir_cnx_ctor_inbound(&env->cnx, &syntax, fd)) return;
	env->quit = false;
	pth_mutex_init(&env->wfd);
	LIST_INIT(&env->subscriptions);
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

static void *serve_cnx(void *arg)
{
	struct cnx_env *env = arg;
	if (! pth_cleanup_push(cnx_env_del, env)) with_error(0, "Cannot add cleanup function") {
		cnx_env_del(env);
		return NULL;
	}
	do {	// read a command and exec it
		mdir_cnx_read(&env->cnx);
		on_error break;
	} while (! env->quit);
	return NULL;
}

/*
 * Main thread
 */

int main(void)
{
	init();
	on_error return EXIT_FAILURE;
	// Run server
	while (! terminate) {
		int fd = server_accept(&server);
		if (fd < 0) {
			error("Cannot accept connection on fd %d, pausing for 5s", server.sock_fd);
			(void)pth_sleep(5);
			continue;
		}
		struct cnx_env *env = cnx_env_new(fd);	// will be destroyed by sub-thread
		unless_error (void)pth_spawn(PTH_ATTR_DEFAULT, serve_cnx, env);
	}
	pth_exit(EXIT_SUCCESS);
}

