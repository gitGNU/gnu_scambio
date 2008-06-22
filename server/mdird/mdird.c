#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pth.h>
#include <string.h>
#include "scambio.h"
#include "conf.h"
#include "daemon.h"
#include "cnx.h"
#include "varbuf.h"
#include "message.h"

/*
 * Data Definitions
 */

static struct cnx_server server;
static sig_atomic_t terminate = 0;

struct cnx_env {
	int fd;
};

/*
 * Init functions
 */

static int init_conf(void)
{
	int err;
	if (0 != (err = conf_set_default_str("SCAMBIO_LOG_DIR", "/var/log"))) return err;
	if (0 != (err = conf_set_default_int("SCAMBIO_PORT", 21654))) return err;
	return 0;
}

static int init_log(void)
{
	int err;
	if (0 != (err = log_begin(conf_get_str("SCAMBIO_LOG_DIR"), "mdird.log"))) return err;
	if (0 != atexit(log_end)) return -1;
	return 0;
}

static void deinit_server(void)
{
	cnx_server_dtor(&server);
}

static int init_server(void)
{
	int err;
	long long port;
	if (0 != (err = cnx_begin())) return err;
	if (0 != atexit(cnx_end)) return -1;
	if (0 != (err = conf_get_int(&port, "SCAMBIO_PORT"))) return err;
	if (0 != (err = cnx_server_ctor(&server, port))) return err;
	if (0 != atexit(deinit_server)) return -1;
	return 0;
}

static int init(void)
{
	int err;
	if (0 != (err = init_conf())) return err;
	if (0 != (err = init_log())) return err;
	if (0 != (err = daemonize())) return err;
	if (! pth_init()) return err;
	if (0 != (err = init_server())) return err;
	return 0;
}

/*
 * Server
 */

#define th_error(fmt, ...) error("%s: " fmt, th_name(), ##__VA_ARGS__)

static char const *th_name(void)
{
	return "TODO";
}

static void cnx_env_del(void *env_)
{
	struct cnx_env *env = env_;
	close(env->fd);
	free(env);
}

static struct cnx_env *cnx_env_new(int fd)
{
	struct cnx_env *env = malloc(sizeof(*env));
	if (! env) {
		th_error("Cannot alloc a cnx_env");
		return NULL;
	}
	env->fd = fd;
	return env;
}

static void *serve_cnx(void *arg)
{
	struct cnx_env *env = arg;
	struct varbuf vb_url, vb_head;
	if (! pth_cleanup_push(cnx_env_del, env)) {
		th_error("Cannot add cleanup function");
		cnx_env_del(env);
		return NULL;
	}
	if (0 != varbuf_ctor(&vb_url, 2000, true)) goto err0;
	if (0 != varbuf_ctor(&vb_head, 5000, true)) goto err1;
	do {	// read a message, each error cutting the cnx (should we ?)
		if (0 != read_url(&vb_url, env->fd)) break;
		if (0 != read_header(&vb_head, env->fd)) break;
		struct header *header = header_new(vb_head.buf);
		if (! header) break;
		insert_message(&vb_url, header);
		header_del(header);
		varbuf_clean(&vb_head);
		varbuf_clean(&vb_url);
	} while (1);
	varbuf_dtor(&vb_head);
err1:
	varbuf_dtor(&vb_url);
err0:
	return NULL;
}

/*
 * Main thread
 */

int main(void)
{
	int err;
	if (0 != (err = init())) {
		fprintf(stderr, "Init error : %s\n", strerror(err));
		return EXIT_FAILURE;
	}
	// Run server
	while (! terminate) {
		int fd = cnx_server_accept(&server);
		struct cnx_env *env = cnx_env_new(fd);	// will be destroyed by sub-thread
		if (env) {
			(void)pth_spawn(PTH_ATTR_DEFAULT, serve_cnx, env);
		}
	}
	pth_exit(EXIT_SUCCESS);
}

