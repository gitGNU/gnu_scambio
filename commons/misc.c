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
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pth.h>
#include <ctype.h>
#include "scambio.h"
#include "misc.h"

int Write(int fd, void const *buf, size_t len)
{
	debug("Write(%d, %p, %zu)", fd, buf, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_write(fd, buf + done, len - done);
		if (ret < 0) {
			if (errno != EINTR) return -errno;
			continue;
		}
		done += ret;
	}
	assert(done == len);
	return 0;
}

int Write_strs(int fd, ...)
{
	va_list ap;
	va_start(ap, fd);
	char const *str;
	int err = 0;
	while (NULL != (str = va_arg(ap, char const *)) && !err) {
		size_t len = strlen(str);
		err = Write(fd, str, len);
	}
	va_end(ap);
	return err;
}

int Read(void *buf, int fd, off_t offset, size_t len)
{
	debug("Read(%p, %d, %zu)", buf, fd, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_pread(fd, buf + done, len - done, offset + done);
		if (ret < 0) {
			if (errno != EINTR) return -errno;
			continue;
		}
		done += ret;
	}
	assert(done == len);
	return 0;
}

static int Mkdir_single(char const *path)
{
	if (0 != mkdir(path, 0744) && errno != EEXIST) {
		error("mkdir '%s' : %s", path, strerror(errno));
		return -errno;
	}
	return 0;
}

int Mkdir(char const *path_)
{
	debug("Mkdir(%s)", path_);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s", path_);
	char *c = path;
	if (! *c) return -EINVAL;
	for (c = path + 1; *c != '\0'; c++) {
		if (*c == '/') {
			*c = '\0';
			int err = Mkdir_single(path);
			if (err) return err;
			*c = '/';
		}
	}
	return Mkdir_single(path);
}

bool line_match(char *restrict line, char *restrict delim)
{
	unsigned c = 0;
	while (delim[c] != '\0') {
		if (delim[c] != line[c]) return false;
		c++;
	}
	while (line[c] != '\0') {
		if (! isspace(line[c])) return false;
		c++;
	}
	return true;
}

void path_push(char path[], char const *next)
{
	char *c = path;
	char const *s = next;
	while (*c != '\0') c++;
	if (c == path || *(c-1) != '/') *c++ = '/';
	do {
		assert(c-path < PATH_MAX);
		*c++ = *s;
	} while (*s++ != '\0');
}

void path_pop(char path[])
{
	char *c = path;
	char *last_sl = path;
	while (*c != '\0') {
		if (*c == '/') last_sl = c;
		c++;
	}
	*last_sl = '\0';
}

