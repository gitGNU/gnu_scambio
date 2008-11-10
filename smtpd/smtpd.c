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
#include "scambio/cnx.h"
#include "varbuf.h"
#include "smtpd.h"
#include "scambio/mdir.h"
#include "server.h"
#include "misc.h"

/*
 * Data Definitions
 */

static struct server server;
static struct mdir_syntax syntax;
static sig_atomic_t terminate = 0;
char my_hostname[256];
struct chn_cnx ccnx;

char const kw_ehlo[] = "ehlo";
char const kw_helo[] = "helo";
char const kw_mail[] = "mail";
char const kw_rcpt[] = "rcpt";
char const kw_data[] = "data";
char const kw_rset[] = "rset";
char const kw_vrfy[] = "vrfy";
char const kw_expn[] = "expn";
char const kw_help[] = "help";
char const kw_noop[] = "noop";
char const kw_quit[] = "quit";

/*
 * Init functions
 */

static void init_conf(void)
{
	conf_set_default_str("SMTPD_LOG_DIR", "/var/log");
	conf_set_default_int("SC_LOG_LEVEL", 3);
	conf_set_default_int("SMTPD_PORT", 25);
	conf_set_default_str("SC_FILED_HOST", "localhost");
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
	conf_set_default_str("SC_FILED_USER", "smtpd");
}

static void init_log(void)
{
	log_begin(conf_get_str("SMTPD_LOG_DIR"), "smtpd.log");
	on_error return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void deinit_server(void)
{
	server_dtor(&server);
}

static void init_syntax(void)
{
	mdir_syntax_ctor(&syntax, false);
	// Register all services
	static struct mdir_cmd_def services[] = {
		{
			.keyword = kw_ehlo, .cb = exec_helo, .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_helo, .cb = exec_helo, .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_mail, .cb = exec_mail, .nb_arg_min = 1, .nb_arg_max = UINT_MAX,
			// no support for extended mail parameters, but we accept them (and ignore them)
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_rcpt, .cb = exec_rcpt, .nb_arg_min = 1, .nb_arg_max = UINT_MAX,
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_data, .cb = exec_data, .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {}, .negseq = false,
		}, {
			.keyword = kw_rset, .cb = exec_rset, .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {}, .negseq = false,
		}, {
			.keyword = kw_vrfy, .cb = exec_vrfy, .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_expn, .cb = exec_expn, .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_help, .cb = exec_help, .nb_arg_min = 0, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING }, .negseq = false,
		}, {
			.keyword = kw_help, .cb = exec_help, .nb_arg_min = 0, .nb_arg_max = UINT_MAX,
			.nb_types = 0, .types = {}, .negseq = false,
		}, {
			.keyword = kw_quit, .cb = exec_quit, .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {}, .negseq = false,
		},
	};
	for (unsigned d=0; d<sizeof_array(services); d++) {
		if_fail (mdir_syntax_register(&syntax, services+d)) return;
	}
}

static void deinit_syntax(void)
{
	mdir_syntax_dtor(&syntax);
}

static void init_server(void)
{
	debug("init server");
	if (0 != gethostname(my_hostname, sizeof(my_hostname))) with_error(errno, "gethostbyname") return;
	if_fail (server_ctor(&server, conf_get_int("SMTPD_PORT"))) return;
	if (0 != atexit(deinit_server)) with_error(0, "atexit") return;
	if_fail (exec_begin()) return;
	if (0 != atexit(exec_end)) with_error(0, "atexit") return;
	if_fail (init_syntax()) return;
	if (0 != atexit(deinit_syntax)) with_error(0, "atexit") return;
}

static void deinit_filed(void)
{
	chn_cnx_dtor(&ccnx);
	chn_end();
}

static void init_filed(void)
{
	if_fail (chn_begin(false)) return;
	char const *host, *serv, *user;
	if_fail (host = conf_get_str("SC_FILED_HOST")) return;
	if_fail (serv = conf_get_str("SC_FILED_PORT")) return;
	if_fail (user = conf_get_str("SC_FILED_USER")) return;
	if_fail (chn_cnx_ctor_outbound(&ccnx, host, serv, user)) return;
	if (0 != atexit(deinit_filed)) with_error(0, "atexit") return;
}

static void init(void)
{
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail (init_conf()) return;
	if_fail (init_log()) return;
	if_fail (mdir_begin()) return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
	if_fail (init_server()) return;
	if_fail (init_filed()) return;
	if_fail (daemonize("sc_smtpd")) return;
}

/*
 * Server
 */

static void cnx_env_del(void *env_)
{
	struct cnx_env *env = env_;
	mdir_cnx_dtor(&env->cnx);
	if (env->domain) free(env->domain);
	if (env->reverse_path) free(env->reverse_path);
	if (env->forward_path) free(env->forward_path);
	free(env);
}

static char *client_host(void)
{
	return "0.0.0.0";	// TODO
}

static void cnx_env_ctor(struct cnx_env *env, int fd)
{
	if_fail (mdir_cnx_ctor_inbound(&env->cnx, &syntax, fd)) return;
	time_t const now = time(NULL);
	struct tm *tm = localtime(&now);
	snprintf(env->client_address, sizeof(env->client_address), "%s", client_host());
	snprintf(env->reception_date, sizeof(env->reception_date), "%04d-%02d-%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
	snprintf(env->reception_time, sizeof(env->reception_time), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
	env->quit = false;
}

static struct cnx_env *cnx_env_new(int fd)
{
	struct cnx_env *env = calloc(1, sizeof(*env));
	if (! env) {
		error("Cannot alloc a cnx_env");
		return NULL;
	}
	if_fail (cnx_env_ctor(env, fd)) {
		free(env);
		env = NULL;
	}
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
	if_fail (Write_strs(env->cnx.fd, "220 ", my_hostname, "\n", NULL)) return NULL;
	while (!env->quit && !is_error()) {	// read a command and exec it
		if_fail (mdir_cnx_read(&env->cnx)) break;
	}
	return NULL;
}

/*
 * Main thread
 */

int main(void)
{
	if (! pth_init()) return EXIT_FAILURE;
	if_fail (init()) {
		pth_kill();
		return EXIT_FAILURE;
	}
	// Run server
	while (! terminate) {
		int fd = server_accept(&server);
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
