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
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "misc.h"

/*
 * Data Definitions
 */

static char const *mdir_files;

/*
 * Init
 */

void mdir_channel_end(void)
{
}

void mdir_channel_begin(void)
{
	if_fail(conf_set_default_str("MDIR_FILES_DIR", "/tmp/mdir/files")) return;
	mdir_files = conf_get_str("MDIR_FILES_DIR");
	if (-1 == atexit(mdir_channel_end)) with_error(0, "atexit(mdir_channel_end)") return;
}

/*
 * Files
 */

void mdir_file_create(char *name, size_t max_size)
{
	unsigned now_tag = (unsigned)time(NULL) & 0xFF;
	char path[PATH_MAX];
	int len = snprintf(path, sizeof(path), "%s/%02x", mdir_files, now_tag);
	Mkdir(path);
	snprintf(path+len, sizeof(path)-len, "/XXXXXX");
	int fd = mkstemp(path);
	if (fd < 0) with_error(errno, "Cannot mkstemp(%s)", path) return;
	close(fd);
	if ((int)max_size <= snprintf(name, max_size, "%02x%s", now_tag, path+len)) with_error(0, "mdir_file_create(): name buffer too short") return;
	return;
}

int mdir_file_open(char *name)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", mdir_files, name);
	int fd = open(path, O_RDWR);
	if (fd < 0) with_error(errno, "Cannot open(%s)", path) return -1;
	return fd;
}

#define COPY_BUF_LEN 10000
void mdir_file_write(int fd, int in, off_t offset, size_t length)
{
	char *buf = malloc(COPY_BUF_LEN);
	if (! buf) with_error(ENOMEM, "malloc buffer") return;
	size_t copied = 0;
	do {
		size_t const to_read = length > COPY_BUF_LEN ? COPY_BUF_LEN:length;
		if_fail (Read(buf, in, to_read)) break;
		if_fail (WriteTo(fd, offset + copied, buf, to_read)) break;
		copied += to_read;
		length -= to_read;
	} while (length);
	free(buf);
}

void mdir_file_read(int fd, int out, off_t offset, size_t length)
{
	char *buf = malloc(COPY_BUF_LEN);
	if (! buf) with_error(ENOMEM, "malloc buffer") return;
	size_t copied = 0;
	do {
		size_t const to_read = length > COPY_BUF_LEN ? COPY_BUF_LEN:length;
		if_fail (ReadFrom(buf, fd, offset + copied, to_read)) break;
		if_fail (Write(out, buf, to_read)) break;
		copied += to_read;
		length -= to_read;
	} while (length);
	free(buf);
}

/*
 * Channels
 */

struct channel *channel_new(void)
{
	return NULL;
}

char const *channel_name(struct channel *chan)
{
	return NULL;
}

/*
 * Transfers
 */

