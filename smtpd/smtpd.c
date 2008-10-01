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
#include <string.h>
#include <time.h>
#include <pth.h>
#include "scambio.h"
#include "daemon.h"
#include "cnx.h"
#include "varbuf.h"
#include "cmd.h"
#include "smtpd.h"
#include "scambio/mdir.h"

/*
 * Data Definitions
 */

static struct cnx_server server;
static sig_atomic_t terminate = 0;
char my_hostname[256];

char const *const kw_ehlo = "ehlo";
char const *const kw_helo = "helo";
char const *const kw_mail = "mail";
char const *const kw_rcpt = "rcpt";
char const *const kw_data = "data";
char const *const kw_rset = "rset";
char const *const kw_vrfy = "vrfy";
char const *const kw_expn = "expn";
char const *const kw_help = "help";
char const *const kw_noop = "noop";
char const *const kw_quit = "quit";

/*
 * Init functions
 */

static void init_conf(void)
{
	conf_set_default_str("SMTPD_LOG_DIR", "/var/log");
	conf_set_default_int("SMTPD_LOG_LEVEL", 3);
	conf_set_default_int("SMTPD_PORT", 25);
}

static void init_log(void)
{
	log_begin(conf_get_str("SMTPD_LOG_DIR"), "smtpd.log");
	on_error return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SMTPD_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init_cmd(void)
{
	debug("init cmd");
	cmd_begin();
	if (0 != atexit(cmd_end)) with_error(0, "atexit") return;
	// Put most used commands at end, so that they end up at the beginning of the commands list
	cmd_register_keyword(kw_ehlo, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_helo, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_mail, 1, UINT_MAX, CMD_STRING, CMD_EOA);	// no support for extended mail parameters, but we accept them (and ignore them)
	cmd_register_keyword(kw_rcpt, 1, UINT_MAX, CMD_STRING, CMD_EOA);	// same
	cmd_register_keyword(kw_data, 0, 0, CMD_EOA);
	cmd_register_keyword(kw_rset, 0, 0, CMD_EOA);
	cmd_register_keyword(kw_vrfy, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_expn, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_help, 0, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_noop, 0, UINT_MAX, CMD_EOA);
	cmd_register_keyword(kw_quit, 0, 0, CMD_EOA);
}

static void deinit_server(void)
{
	cnx_server_dtor(&server);
	cnx_end();
}

static void init_server(void)
{
	debug("init server");
	if (0 != gethostname(my_hostname, sizeof(my_hostname))) with_error(errno, "gethostbyname") return;
	if_fail (cnx_begin()) return;
	if_fail (cnx_server_ctor(&server, conf_get_int("SMTPD_PORT"))) return;
	if (0 != atexit(deinit_server)) with_error(0, "atexit") return;
	if_fail (exec_begin()) return;
	if (0 != atexit(exec_end)) with_error(0, "atexit") return;
}

static void init(void)
{
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail (init_conf()) return;
	if_fail (init_log()) return;
	if_fail (mdir_begin()) return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
	if_fail (init_cmd()) return;
	if_fail (daemonize()) return;
	if_fail (init_server()) return;
}

/*
 * Server
 */

static void cnx_env_del(void *env_)
{
	struct cnx_env *env = env_;
	close(env->fd);
	if (env->domain) free(env->domain);
	if (env->reverse_path) free(env->reverse_path);
	if (env->forward_path) free(env->forward_path);
	free(env);
}

static char *client_host(void)
{
	return "0.0.0.0";	// TODO
}

static struct cnx_env *cnx_env_new(int fd)
{
	struct cnx_env *env = calloc(1, sizeof(*env));
	if (! env) {
		error("Cannot alloc a cnx_env");
		return NULL;
	}
	env->fd = fd;
	time_t const now = time(NULL);
	struct tm *tm = localtime(&now);
	snprintf(env->client_address, sizeof(env->client_address), "%s", client_host());
	snprintf(env->reception_date, sizeof(env->reception_date), "%04d-%02d-%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	snprintf(env->reception_time, sizeof(env->reception_time), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
	return env;
}

static void *serve_cnx(void *arg)
{
	struct cnx_env *env = arg;
	if (! pth_cleanup_push(cnx_env_del, env)) {
		error("Cannot add cleanup function");
		cnx_env_del(env);
		return NULL;
	}
	bool quit = false;
	if_fail (answer(env, 220, my_hostname)) return NULL;
	while (! quit && !is_error()) {	// read a command and exec it
		struct cmd cmd;
		if_fail (cmd_read(&cmd, env->fd)) break;
		debug("Read keyword '%s'", cmd.keyword);
		if (cmd.keyword == kw_ehlo) {
			exec_ehlo(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_helo) {
			exec_helo(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_mail) {
			exec_mail(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_rcpt) {
			exec_rcpt(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_data) {
			exec_data(env);
		} else if (cmd.keyword == kw_rset) {
			exec_rset(env);
		} else if (cmd.keyword == kw_vrfy) {
			exec_vrfy(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_expn) {
			exec_expn(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_help) {
			exec_helo(env, cmd.nb_args > 0 ? cmd.args[0].val.string:NULL);
		} else if (cmd.keyword == kw_noop) {
			exec_noop(env);
		} else if (cmd.keyword == kw_quit) {
			exec_quit(env);
			quit = true;
		} else {
			// ignore ?
		}
		cmd_dtor(&cmd);
	}
	return NULL;
}

/*
 * Main thread
 */

int main(void)
{
	int err;
	if (! pth_init()) return EXIT_FAILURE;
	if_fail (init()) {
		fprintf(stderr, "Init error : %s\n", strerror(-err));
		pth_kill();
		return EXIT_FAILURE;
	}
	// Run server
	while (! terminate) {
		int fd = cnx_server_accept(&server);
		if (fd < 0) {
			error("Cannot accept connection on fd %d, pausing for 5s", server.sock_fd);
			(void)pth_sleep(5);
			continue;
		}
		struct cnx_env *env = cnx_env_new(fd);	// will be destroyed by sub-thread
		if (env) {
			(void)pth_spawn(PTH_ATTR_DEFAULT, serve_cnx, env);
		}
	}
	pth_kill();
	return EXIT_SUCCESS;
}
