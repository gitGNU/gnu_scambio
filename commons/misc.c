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
#include <unistd.h>
#include <ctype.h>
#include <pth.h>
#include "scambio.h"
#include "misc.h"

void Write(int fd, void const *buf, size_t len)
{
	debug("Write(%d, %p, %zu)", fd, buf, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_write(fd, buf + done, len - done);
		if (ret < 0) {
			// FIXME: truncate on short writes
			if (errno != EINTR) with_error(errno, "Cannot write %zu bytes", len-done) return;
			continue;
		}
		done += ret;
	}
	assert(done == len);
}

void Write_strs(int fd, ...)
{
	va_list ap;
	va_start(ap, fd);
	char const *str;
	while (NULL != (str = va_arg(ap, char const *)) && !is_error()) {
		debug("will write string '%s'", str);
		size_t len = strlen(str);
		Write(fd, str, len);
	}
	va_end(ap);
}

void ReadFrom(void *buf, int fd, off_t offset, size_t len)
{
	debug("ReadFrom(%p, %d, from=%u, len=%zu)", buf, fd, (unsigned)offset, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_pread(fd, buf + done, len - done, offset + done);
		if (ret < 0) {
			if (errno != EINTR) with_error(errno, "Cannot pth_pread") return;
			continue;
		} else if (ret == 0) with_error(0, "EOF") return;
		done += ret;
	}
	assert(done == len);
}

void Read(void *buf, int fd, size_t len)
{
	debug("Read(%p, %d, %zu)", buf, fd, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_read(fd, buf + done, len - done);
		if (ret < 0) {
			if (errno != EINTR) with_error(errno, "Cannot pth_read") return;
			continue;
		}
		done += ret;
	}
	assert(done == len);
}

void WriteTo(int fd, off_t offset, void *buf, size_t len)
{
	debug("WriteTo(%d, %u, %p, %zu)", fd, (unsigned)offset, buf, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_pwrite(fd, buf + done, len - done, offset);
		if (ret < 0) {
			// FIXME: truncate on short writes
			if (errno != EINTR) with_error(errno, "Cannot write %zu bytes", len-done) return;
			continue;
		}
		done += ret;
	}
	assert(done == len);
}

void Copy(int dst, int src)
{
	debug("Copy from %d to %d", src, dst);
	char byte;
	do {
		ssize_t ret = pth_read(src, &byte, 1);
		if (ret < 0) {
			if (errno != EINTR) with_error(errno, "Cannot pth_read") return;
			continue;
		}
		if (ret == 0) return;
		Write(dst, &byte, 1);
		on_error return;
	} while (1);
}

static void Mkdir_single(char const *path)
{
	if (0 != mkdir(path, 0744) && errno != EEXIST) {
		error_push(errno, "mkdir '%s'", path);
	}
}

void Mkdir(char const *path_)
{
	debug("Mkdir(%s)", path_);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s", path_);
	char *c = path;
	if (! *c) with_error(EINVAL, "Cannot mkdir empty path") return;
	for (c = path + 1; *c != '\0'; c++) {
		if (*c == '/') {
			*c = '\0';
			Mkdir_single(path);
			on_error return;
			*c = '/';
		}
	}
	Mkdir_single(path);
}

void Mkdir_for_file(char *path)
{
	char *last_sep = NULL;
	for (char *c = path; *c; c++) {
		if (*c == '/') last_sep = c;
	}
	if (last_sep) {
		*last_sep = '\0';
		Mkdir(path);
		*last_sep = '/';
	}
}

void Make_path(char *buf, size_t buf_size, ...)
{
	va_list ap;
	va_start(ap, buf_size);
	char const *str;
	size_t buf_len = 0;
	while (NULL != (str = va_arg(ap, char const *)) && buf_len < buf_size) {
		buf_len += snprintf(buf + buf_len, buf_size - buf_len, "%s%s",
			str[0] != '/' && (buf_len == 0 || buf[buf_len-1] != '/') ? "/":"",
			str);
	}
	va_end(ap);
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

off_t filesize(int fd)
{
	off_t size = lseek(fd, 0, SEEK_END);
	if ((off_t)-1 == size) error_push(errno, "lseek");
	return size;
}


