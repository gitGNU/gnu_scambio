#include <stdlib.h>
#include <stdio.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/cmd.h"
#include "scambio/cnx.h"
#include "daemon.h"
#include "server.h"

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
			.keyword = kw_creat, .cb = NULL,            .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {},
		}, {
			.keyword = kw_write, .cb = NULL,            .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_read,  .cb = NULL,            .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_quit,  .cb = NULL,            .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {},
		},
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
	if_fail (init_server()) return;
	if (0 != atexit(deinit_syntax)) with_error(0, "atexit") return;
}

/*
 * Single connection service
 */

static void cnx_env_ctor(struct cnx_env *env, int fd)
{
	if_fail (chn_cnx_ctor_inbound(&env->cnx, &syntax, fd)) return;
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
	chn_cnx_dtor(&env->cnx);
}

static void cnx_env_del(struct cnx_env *env)
{
	cnx_env_dtor(env);
	free(env);
}

static void *serve_cnx(void *arg)
{
	struct cnx_env *env = arg;
	if (! pth_cleanup_push(cnx_env_del, env)) with_error(0, "Cannot add cleanup function") {
		cnx_env_del(env);
		return NULL;
	}
	do {
		pth_yield(NULL);
		/* write what's needed, while the chn_cnx reader thread handle reading
		 * the fd and execing our callbacks.
		 * The reader callbacks never answer queries (so must not call mdir_cnx_answer)
		 * except for the create one (which is trivial and makes no pause).
		 * We are concerned only by feeding/fetching the transferts, ie reading from
		 * localfile or live streams and writing into the tx, or the other way round
		 * (reading from a tx and writing onto a file or stream), untill the tx is over
		 * and we the writer reply to the initial tx query.
		 * The fact that, when writing/reading a file, the writer for this cnx is paused
		 * should not be a problem (the other threads still go on).
		 * We use the abstraction of a stream, which have a name (the name used as ressource
		 * locator), to which we can append data or read from a cursor. Each stream may have
		 * at most one writer, but can have many readers. A reader may be the writer thread
		 * of this connection or another one.
		 *
		 * A run goes like this :
		 *
		 * - for all our ongoing, associated tx (a associated tx is a tx + a link to the stream)
		 *   - if it's completed, send the final answer, deassociat to the stream and delete it
		 *   - if we're receiver,
		 *     - try to read something and add this into the sorted by offset sequence of boxes received so far
		 *     - handle completed part of this sequence to the associated stream
		 *     - if we should have received a fragment long ago, ask for it and renew this timeout date
		 *   - if we're transmiter,
		 *     - read from the associated stream and write to the tx.
		 * - for all our ongoing, not yet associated tx, find/create the associated stream.
		 * - relieve the CPU untill ... something happen (a tx receive something or is created,
		 *   or a stream outputing to one of our tx have something to offer. We can do this by
		 *   signaling ? Or we could use one thread by file backed stream, to read file and write
		 *   a ref of the box to every associated tx so that we writer thread can block on reading
		 *   the fd (or a condition on the chn_cnx). Also, stream we write to may be threads
		 *   that wait untill some of there input is there (condition) and write it. Thus we
		 *   do not need a writer thread anymore ! but streams must have both a reader and a writer
		 *   thread in general. Not sure how to design it.
		 */
	} while (! env->quit);
	return NULL;
}

/*
 * Main
 */

int main(int nb_args, char **args)
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
