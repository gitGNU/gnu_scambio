#include <stdlib.h>
#include <stdio.h>
#include "daemon.h"
#include "conf.h"

/*
 * Log files handling
 */

static int log_level;
static char log_filename[PATH_MAX];
FILE *log_file = NULL;

static int reopen_log(void)
{
	if (log_file) {
		fclose(log_file);
		log_file = NULL;
	}
	log_file = fopen(log_filename, "a");
	if (! log_file) return -1;
	fh_fatal = log_file;
	fh_err  = log_level > 0 ? log_file : NULL;
	fh_warn = log_level > 1 ? log_file : NULL;
	fh_info = log_level > 2 ? log_file : NULL;
	fh_dbg  = log_level > 3 ? log_file : NULL;
	info("Start log at level %d", log_level);
	return 0;
}

static int fetch_conf(void)
{
	char const *log_path = conf_get_str("SCAMBIO_LOG_PATH");
	if (! log_path) return -ENOENT;
	snprintf(log_filename, sizeof(log_filename), "%s/scambio.log", log_path);
	log_level = 3;
	long long value;
	switch (conf_get_int(&value, "SCAMBIO_LOG_LEVEL")) {
		case 0:
			log_level = value;
			break;
		case -ENOENT:
			break;
		default:
			return -1;
	}
	return 0;
}

static int handle_HUP(void)
{
	// TODO
	return 0;
}

static int init_log(void)
{
	if (0 != fetch_conf()) return -1;
	if (0 != handle_HUP()) return -1;
	if (0 != reopen_log()) return -1;
	return 0;
}

/*
 * Daemonization
 */

int daemonize(void)
{
	int err = init_log();
	if (err) return err;
	return make_background();
}
