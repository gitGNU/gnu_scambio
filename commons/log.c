#include <limits.h>
#include <errno.h>
#include "scambio.h"
#include "log.h"

/*
 * Public Functions
 */

FILE *log_file = NULL;
int log_level = 3;
static char file_path[PATH_MAX];

static void close_log(void)
{
	if (log_file) {
		(void)fclose(log_file);
		log_file = NULL;
	}
}
static int open_log(void)
{
	close_log();
	log_file = fopen(file_path, "a");
	if (! log_file) return -errno;
	info("Start login with log level = %d", log_level);
	return 0;
}

int log_begin(char const *dirname, char const *filename)
{
	snprintf(file_path, sizeof(file_path), "%s/%s", dirname, filename);
	return open_log();
}

void log_end(void)
{
	close_log();
}

