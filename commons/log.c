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
#include <limits.h>
#include <errno.h>
#include "scambio.h"
#include "log.h"
#include "misc.h"

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
	if (!dirname || !filename) return 0;
	if_fail (Mkdir(dirname)) return 0;
	snprintf(file_path, sizeof(file_path), "%s/%s.log", dirname, filename);
	return open_log();
}

void log_end(void)
{
	close_log();
}

void log_print(char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (log_file) {
		char buf[24];
		time_t t = time(NULL);
		(void)strftime(buf, sizeof(buf), "%F %T", localtime(&t));
		fprintf(log_file, "%s: ", buf);
		vfprintf(log_file, fmt, ap);
		fprintf(log_file, "\n");
		fflush(log_file);
	} else {
		vprintf(fmt, ap);
		printf("\n");
	}
	va_end(ap);
}

