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
#include <netdb.h>
#include <pth.h>
#include "scambio.h"
#include "misc.h"

static bool retryable(int err)
{
	return err == EINTR || err == EAGAIN;
}

void Write(int fd, void const *buf, size_t len)
{
	debug("Write(%d, %p, %zu)", fd, buf, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_write(fd, buf + done, len - done);
		if (ret < 0) {
			// FIXME: truncate on short writes
			if (! retryable(errno)) with_error(errno, "Cannot write %zu bytes", len-done) return;
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
			if (! retryable(errno)) with_error(errno, "Cannot pth_pread") return;
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
			if (! retryable(errno)) with_error(errno, "Cannot pth_read") return;
			continue;
		}
		done += ret;
	}
	assert(done == len);
}

void WriteTo(int fd, off_t offset, void const *buf, size_t len)
{
	debug("WriteTo(%d, %u, %p, %zu)", fd, (unsigned)offset, buf, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_pwrite(fd, buf + done, len - done, offset);
		if (ret < 0) {
			// FIXME: truncate on short writes
			if (! retryable(errno)) with_error(errno, "Cannot write %zu bytes", len-done) return;
			continue;
		}
		done += ret;
	}
	assert(done == len);
}

void Copy(int dst, int src)
{
	debug("Copy from %d to %d", src, dst);
#	define BUFFER_SIZE 65536
	char *buffer = malloc(BUFFER_SIZE);
	if (! buffer) with_error(ENOMEM, "Cannot alloc buffer for copy") return;
	do {
		ssize_t ret = pth_read(src, buffer, BUFFER_SIZE);
		if (ret < 0) {
			if (! retryable(errno)) with_error(errno, "Cannot pth_read") break;
			continue;
		}
		if (ret == 0) break;
		if_fail (Write(dst, buffer, ret)) break;
	} while (1);
	free(buffer);
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

void Mkdir_for_file(char const *path)
{
	char p[PATH_MAX];
	int last_sep = -1;
	for (unsigned c = 0; path[c]; c++) {
		if (path[c] == '/') last_sep = c;
		p[c] = path[c];
	}
	if (last_sep >= 0) {
		p[last_sep] = '\0';
		Mkdir(p);
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
	if ((off_t)-1 == size) error_push(errno, "lseek(end)");
	if ((off_t)-1 == lseek(fd, 0, SEEK_SET)) error_push(errno, "lseek(start)");
	debug("filesize of fd %d is %u", fd, (unsigned)size);
	return size;
}

char *Strdup(char const *orig)
{
	char *ret = strdup(orig);
	if (! ret) with_error(errno, "strdup") return NULL;
	return ret;
}

void FreeIfSet(char **ptr)
{
	if (NULL == *ptr) return;
	free(*ptr);
	*ptr = NULL;
}

static char const *gaierr2str(int err)
{
	switch (err) {
		case EAI_SYSTEM:     return strerror(errno);
		case EAI_ADDRFAMILY: return "No addr in family";
		case EAI_BADFLAGS:   return "Bad flags";
		case EAI_FAIL:       return "DNS failed";
		case EAI_FAMILY:     return "Bad family";
		case EAI_MEMORY:     return "No mem";
		case EAI_NODATA:     return "No address";
		case EAI_NONAME:     return "No such name";
		case EAI_SERVICE:    return "Not in this socket type";
		case EAI_SOCKTYPE:   return "Bad socket type";
	}
	return "Unknown error";
}

int Connect(char const *host, char const *service)
{
	int fd = -1;
	// Resolve hostname into sockaddr
	debug("Connecting to %s:%s", host, service);
	struct addrinfo *info_head, *ainfo;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;	// AF_UNSPEC to allow IPv6
	hints.ai_socktype = SOCK_STREAM;	// TODO: configure this
	int err;
	if (0 != (err = getaddrinfo(host, service, &hints, &info_head))) {
		// TODO: check that freeaddrinfo is not required in this case
		with_error(0, "Cannot getaddrinfo : %s", gaierr2str(err)) return -1;
	}
	err = ENOENT;
	for (ainfo = info_head; ainfo; ainfo = ainfo->ai_next) {
		fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
		if (fd == -1) continue;
		if (0 == connect(fd, ainfo->ai_addr, ainfo->ai_addrlen)) {
			info("Connected to %s:%s", host, service);
			break;
		}
		err = errno;
		(void)close(fd);
		fd = -1;
	}
	if (! ainfo) error_push(err, "No suitable address found for host %s:%s", host, service);
	freeaddrinfo(info_head);
	return fd;
}

char const *Basename(char const *path)
{
	for (char const *c = path; *c; c++) {
		if (*c == '/') path = c+1;
	}
	return path;
}
