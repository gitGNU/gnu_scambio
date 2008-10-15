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
#include "cnx.h"
#include "varbuf.h"
#include "cmd.h"
#include "mdird.h"
#include "sub.h"

/*
 * Data Definitions
 */

static struct cnx_server server;
static sig_atomic_t terminate = 0;

struct cmd_parser parser;
static char const *const kw_auth  = "auth";
static char const *const kw_sub   = "sub";
static char const *const kw_unsub = "unsub";
static char const *const kw_put   = "put";
static char const *const kw_rem   = "rem";
static char const *const kw_quit  = "quit";

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

static void cmd_end(void)
{
	cmd_parser_dtor(&parser);
}

static void init_cmd(void)
{
	debug("init cmd");
	cmd_parser_ctor(&parser);
	if (0 != atexit(cmd_end)) with_error(0, "atexit") return;
	cmd_register_keyword(&parser, kw_quit,  0, 0, CMD_EOA);
	cmd_register_keyword(&parser, kw_auth,  1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(&parser, kw_unsub, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(&parser, kw_sub,   2, 2, CMD_STRING, CMD_INTEGER, CMD_EOA);
	cmd_register_keyword(&parser, kw_rem,   1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(&parser, kw_put,   1, 1, CMD_STRING, CMD_EOA);
}

static void deinit_server(void)
{
	cnx_server_dtor(&server);
}

static void init_server(void)
{
	debug("init server");
	cnx_begin();
	on_error return;
	if (0 != atexit(cnx_end)) with_error(0, "atexit") return;
	cnx_server_ctor(&server, conf_get_int("MDIRD_PORT"));
	on_error return;
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
	init_cmd();  on_error return;
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
	close(env->fd);
	env->user = NULL;
	free(env);
}

static struct cnx_env *cnx_env_new(int fd)
{
	struct cnx_env *env = malloc(sizeof(*env));
	if (! env) with_error(ENOMEM, "Cannot alloc a cnx_env") return NULL;
	env->fd = fd;
	env->user = NULL;
	pth_mutex_init(&env->wfd);
	LIST_INIT(&env->subscriptions);
	return env;
}

static void *serve_cnx(void *arg)
{
	struct cnx_env *env = arg;
	if (! pth_cleanup_push(cnx_env_del, env)) with_error(0, "Cannot add cleanup function") {
		cnx_env_del(env);
		return NULL;
	}
	bool quit = false;
	do {	// read a command and exec it
		struct cmd cmd;
		cmd_read(&parser, &cmd, env->fd);
		on_error break;
		pth_mutex_acquire(&env->wfd, FALSE, NULL);
		if (cmd.keyword == kw_auth) {
			exec_auth(env, cmd.seq, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_sub) {
			exec_sub(env, cmd.seq, cmd.args[0].val.string, cmd.args[1].val.integer);
		} else if (cmd.keyword == kw_unsub) {
			exec_unsub(env, cmd.seq, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_put) {
			exec_put(env, cmd.seq, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_rem) {
			exec_rem(env, cmd.seq, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_quit) {
			exec_quit(env, cmd.seq);
			quit = true;
		}
		pth_mutex_release(&env->wfd);
		cmd_dtor(&cmd);
		on_error break;
	} while (! quit);
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
		int fd = cnx_server_accept(&server);
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

