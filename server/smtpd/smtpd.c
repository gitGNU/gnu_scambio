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
#include "header.h"
#include "smtpd.h"

/*
 * Data Definitions
 */

static struct cnx_server server;
static sig_atomic_t terminate = 0;

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

static int init_conf(void)
{
	int err;
	if (0 != (err = conf_set_default_str("SMTPD_LOG_DIR", "/var/log"))) return err;
	if (0 != (err = conf_set_default_int("SMTPD_LOG_LEVEL", 3))) return err;
	if (0 != (err = conf_set_default_int("SMTPD_PORT", 25))) return err;
	return 0;
}

static int init_log(void)
{
	int err;
	if (0 != (err = log_begin(conf_get_str("SMTPD_LOG_DIR"), "smtpd.log"))) return err;
	debug("init log");
	if (0 != atexit(log_end)) return -1;
	log_level = conf_get_int("SMTPD_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
	return 0;
}

static int init_cmd(void)
{
	debug("init cmd");
	cmd_begin();
	if (0 != atexit(cmd_end)) return -1;
	// Put most used commands at end, so that they end up at the beginning of the commands list
	cmd_register_keyword(kw_ehlo, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_helo, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_mail, 1, 99, CMD_STRING, CMD_EOA);	// no support for extended mail parameters, but we accept them (and ignore them)
	cmd_register_keyword(kw_rcpt, 1, 99, CMD_STRING, CMD_EOA);	// same
	cmd_register_keyword(kw_data, 0, 0, CMD_EOA);
	cmd_register_keyword(kw_rset, 0, 0, CMD_EOA);
	cmd_register_keyword(kw_vrfy, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_expn, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_help, 0, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(kw_noop, 0, 99, CMD_EOA);
	cmd_register_keyword(kw_quit, 0, 0, CMD_EOA);
	return 0;
}

static void deinit_server(void)
{
	cnx_server_dtor(&server);
	cnx_end();
}

static int init_server(void)
{
	int err;
	debug("init server");
	if (0 != (err = cnx_begin())) return err;
	if (0 != (err = cnx_server_ctor(&server, conf_get_int("SMTPD_PORT")))) return err;
	if (0 != atexit(deinit_server)) return -1;
	if (0 != (err = exec_begin())) return err;
	if (0 != atexit(exec_end)) return -1;
	return 0;
}

static int init(void)
{
	int err;
	if (0 != (err = init_conf())) return err;
	if (0 != (err = init_log())) return err;
	if (0 != (err = init_cmd())) return err;
	if (0 != (err = daemonize())) return err;
	if (! pth_init()) return err;
	if (0 != (err = init_server())) return err;
	return 0;
}

/*
 * Server
 */

static void cnx_env_del(void *env_)
{
	struct cnx_env *env = env_;
	close(env->fd);
	if (env->h) header_del(env->h);
	free(env);
}

static char *client_host(void)
{
	return "0.0.0.0";	// TODO
}

static char *my_host(void)
{
	return "MeMyselfAndI";	// TODO
}

static struct cnx_env *cnx_env_new(int fd)
{
	struct cnx_env *env = malloc(sizeof(*env));
	if (! env) {
		error("Cannot alloc a cnx_env");
		return NULL;
	}
	env->fd = fd;
	time_t now = time(NULL);
	snprintf(env->start_head, sizeof(env->start_head),
		"Type: email\n"
		"Received: from %s\n"
		"  by %s SMTPD "TOSTR(VERSION)"; %s",
		client_host(),
		my_host(),
		ctime(&now));
	env->h = header_new(env->start_head);
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
	int err = 0;
	bool quit = false;
	do {	// read a command and exec it
		struct cmd cmd;
		if (0 != (err = cmd_read(&cmd, true, env->fd))) break;
		if (cmd.keyword == kw_ehlo) {
			err = exec_ehlo(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_helo) {
			err = exec_helo(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_mail) {
			err = exec_mail(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_rcpt) {
			err = exec_rcpt(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_data) {
			err = exec_data(env);
		} else if (cmd.keyword == kw_rset) {
			err = exec_rset(env);
		} else if (cmd.keyword == kw_vrfy) {
			err = exec_vrfy(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_expn) {
			err = exec_expn(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_help) {
			err = exec_helo(env, cmd.args[0].val.string);
		} else if (cmd.keyword == kw_noop) {
			err = exec_noop(env);
		} else if (cmd.keyword == kw_quit) {
			err = exec_quit(env);
			quit = true;
		}
		cmd_dtor(&cmd);
	} while (! err && ! quit);
	return NULL;
}

/*
 * Main thread
 */

int main(void)
{
	int err;
	if (0 != (err = init())) {
		fprintf(stderr, "Init error : %s\n", strerror(-err));
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
	pth_exit(EXIT_SUCCESS);
}
