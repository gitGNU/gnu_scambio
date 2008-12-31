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
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pth.h>
#include "scambio.h"
#include "options.h"
#include "scambio/channel.h"

static enum action { SEND, GET, NONE } action = NONE;
static char const *resource;
static char const *filename;
static char const *host = "localhost";
static char const *port = DEFAULT_FILED_PORT;
static struct chn_cnx *cnx;

static void do_send(void)
{
	char ref[PATH_MAX];
	chn_send_file_request(cnx, filename, ref);
	unless_error puts(ref);
}

static void do_get(void)
{
	char name[PATH_MAX];
	chn_get_file(cnx, name, resource);
	unless_error puts(name);
}

static void wait_complete(void)
{
	debug("waiting...");
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
	while (! chn_cnx_all_tx_done(cnx)) pth_nanosleep(&ts, NULL);
}

int main(int nb_args, char const**args)
{
	log_begin(NULL, NULL);
	atexit(log_end);
	if (! pth_init()) exit(EXIT_FAILURE);
	error_begin();
	atexit(error_end);
	conf_set_default_int("SC_LOG_LEVEL", 3);
	log_level = conf_get_int("SC_LOG_LEVEL");

	if (0 == strcmp(args[0], "sc_sendfile")) action = SEND;
	else if (0 == strcmp(args[0], "sc_getfile")) action = GET;
	struct option options[] = {
		{
			'a', "action",   OPT_ENUM,   &action,
			"what to do", { .opt_enum = (char const *const[]){ "send", "get", NULL } }
		},	{
			'r', "resource", OPT_STRING, &resource,
			"resource name", {},
		}, {
			'f', "file",     OPT_STRING, &filename,
			"local file name to send", {},
		},
	};
	if_fail (option_parse(nb_args, args, options, sizeof_array(options))) return EXIT_FAILURE;
	if (action == NONE) option_missing("action");
	if (action == GET && !resource) option_missing("resource");
	if (action == SEND && !filename) option_missing("file");

	// Connect
	if_fail (chn_init(false)) return EXIT_FAILURE;
	conf_set_default_str("SC_FILED_HOST", host);
	conf_set_default_str("SC_FILED_PORT", port);
	if_fail (cnx = chn_cnx_new_outbound(conf_get_str("SC_FILED_HOST"), conf_get_str("SC_FILED_PORT"), conf_get_str("SC_USERNAME"))) {
		return EXIT_FAILURE;
	}

	switch (action) {
		case NONE:  break;
		case SEND:  do_send();  break;
		case GET:   do_get();   break;
	}

	unless_error wait_complete();
	error_save();
	chn_cnx_del(cnx);
	cnx = NULL;
	error_restore();

	return is_error() ? EXIT_FAILURE:EXIT_SUCCESS;
}
