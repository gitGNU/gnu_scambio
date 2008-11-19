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
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "scambio.h"
#include "persist.h"

static int open_or_create(char const *fname, size_t size)
{
	int fd = open(fname, O_RDWR);
	if (fd >= 0) return fd;	// FIXME: check size nonetheless
	if (errno != ENOENT) with_error(errno, "Cannot open '%s'", fname) return -1;
	fd = open(fname, O_RDWR|O_CREAT|O_EXCL, 0660);
	if (fd < 0) with_error(errno, "Cannot create '%s'", fname) return -1;
	if (0 != ftruncate(fd, size)) {
		error_push(errno, "Cannot truncate '%s'", fname);
		(void)close(fd);
		return -1;
	}
	return fd;
}

void persist_ctor(struct persist *p, size_t size, char const *fname)
{
	p->size = size;
	int fd = open_or_create(fname, size);
	on_error return;
	p->data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (MAP_FAILED == p->data) error_push(errno, "Cannot mmap(file='%s', size=%zu)", fname, size);
	(void)close(fd);
}

void persist_dtor(struct persist *p)
{
	(void)munmap(p->data, p->size);
}

