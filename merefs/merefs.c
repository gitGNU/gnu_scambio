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
/*
 * This tools tracks a mdir to maintain a user directory tree in synch.
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <pth.h>
#include "daemon.h"
#include "options.h"
#include "persist.h"
#include "merefs.h"

/*
 * Data Definitions
 */

char const *mdir_name;
char const *local_path;
unsigned local_path_len;
struct mdir *mdir;
static struct persist last_time_stamp;
bool quit = false;
struct chn_cnx ccnx;

/*
 * Init
 */

static void init_conf(void)
{
	conf_set_default_str("SC_LOG_DIR", "/var/log/scambio");
	conf_set_default_int("SC_LOG_LEVEL", 3);
	conf_set_default_str("SC_FILED_HOST", "localhost");
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
}

static void init_log(void)
{
	if_fail(log_begin(conf_get_str("SC_LOG_DIR"), "merefs")) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Setting log level to %d", log_level);
}

static void deinit_chn(void)
{
	chn_cnx_dtor(&ccnx);
	chn_end();
}

static void init_chn(void)
{
	if_fail (chn_begin(false)) return;
	if_fail (chn_cnx_ctor_outbound(&ccnx, conf_get_str("SC_FILED_HOST"), conf_get_str("SC_FILED_PORT"), conf_get_str("SC_USERNAME"))) return;
	if (0 != atexit(deinit_chn)) with_error(0, "atexit") return;
}

static void init(void)
{
	if (! pth_init()) with_error(0, "Cannot init PTH") return;
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail(init_conf()) return;
	if_fail(init_log()) return;
	if_fail(mdir_begin()) return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
	if_fail (files_begin()) return;
	if_fail (init_chn()) return;
}

/*
 * Go
 */

time_t last_run_start(void)
{
	return *(time_t *)persist_read(&last_time_stamp);
}

static void loop(void)
{
	start_read_mdir();	// Will read the whole mdir and create an entry (on unmatched list) for each file
	while (! quit) {
		time_t current_run_start = time(NULL);
		unmatch_all();
		if_fail (reread_mdir()) break;	// Will append to unmatched list the new entry
		if_fail (traverse_local_path()) break;	// Will match each local file against its mdir entry
		if_fail (create_unmatched_files()) break;	// Will add to local tree the new entries
		persist_write(&last_time_stamp, &current_run_start);
		pth_sleep(1);	// will schedule other threads and prevent us from eating the CPU
	}
}

static void last_ts_save(void)
{
	persist_dtor(&last_time_stamp);
}

int main(int nb_args, char const **args)
{
	if_fail(init()) return EXIT_FAILURE;
	mdir_name = conf_get_str("SC_MEREFS_MDIR");
	local_path = conf_get_str("SC_MEREFS_PATH");
	struct option const options[] = {
		{
			'm', "mdir", OPT_STRING, &mdir_name, "The mdir path to track", {},
		}, {
			'p', "path", OPT_STRING, &local_path, "The local path to synch it with", {},
		},
	};
	if_fail (option_parse(nb_args, args, options, sizeof_array(options))) return EXIT_FAILURE;
	if (! mdir_name) option_missing("mdir");
	if (! local_path) option_missing("path");
	if_fail (daemonize("sc_merefs")) return EXIT_FAILURE;
	debug("Keeping mdir '%s' in synch with path '%s'", mdir_name, local_path);
	local_path_len = strlen(local_path);
	if_fail (mdir = mdir_lookup(mdir_name)) return EXIT_FAILURE;
	// Init time_stamp file
	char persistant_ts_file[PATH_MAX];
	snprintf(persistant_ts_file, sizeof(persistant_ts_file), "%s/.sc_merefs_ts", local_path);
	time_t default_ts = 0;
	if_fail (persist_ctor(&last_time_stamp, sizeof(default_ts), persistant_ts_file, &default_ts)) return EXIT_FAILURE;
	atexit(last_ts_save);

	if_fail (loop()) return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

