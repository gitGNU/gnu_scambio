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
#ifndef LOG_H_080527
#define LOG_H_080527

#include <stdio.h>
#include <time.h>
#include <stdlib.h>	// abort

extern FILE *log_file;
extern int log_level;
int log_begin(char const *dirname, char const *filename);
void log_end(void);

#define log_print(fmt, ...) do if (log_file) { \
	char buf[24]; \
	time_t t = time(NULL); \
	(void)strftime(buf, sizeof(buf), "%F %T", localtime(&t)); \
	fprintf(log_file, "%s: "fmt "\n", buf, ##__VA_ARGS__); \
	fflush(log_file); \
} while(0)

#define error(...)      do if (log_level > 0) log_print("ERR: " __VA_ARGS__); while(0)
#define warning(...)    do if (log_level > 1) log_print("WRN: " __VA_ARGS__); while(0)
#define info(...)       do if (log_level > 2) log_print("NFO: " __VA_ARGS__); while(0)
#ifndef NDEBUG
#define debug(fmt, ...) do if (log_level > 3) log_print("DBG: %s: " fmt, __func__, ##__VA_ARGS__); while(0)
#else
#define debug(...)
#endif
#define fatal(...)      do { log_print("FATAL: " __VA_ARGS__); abort(); } while(0)

#endif
