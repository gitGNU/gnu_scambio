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
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pth.h>
#include "varbuf.h"
#include "misc.h"

extern inline void varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable);
extern inline void varbuf_dtor(struct varbuf *vb);

static void varbuf_make_room(struct varbuf *vb, size_t new_size)
{
	new_size++;	// for the nul byte we add inconditionaly
	if (vb->actual >= new_size) return;
	void *new_buf = realloc(vb->buf, new_size);
	if (! new_buf) with_error(ENOMEM, "Cannot realloc varbuf to %zu bytes", new_size) return;
	if (new_buf != vb->buf && !vb->relocatable) {
		error_push(ENOMEM, "Cannot extend not relocatable varbuf");
		free(new_buf);
		return;
	}
	vb->buf = new_buf;
	vb->actual = new_size;
}

void varbuf_append(struct varbuf *vb, size_t size, void const *buf)
{
	size_t pos = vb->used;
	varbuf_put(vb, size);	// will place a nul byte at the end
	on_error return;
	memcpy(vb->buf + pos, buf, size);
}

void varbuf_append_strs(struct varbuf *vb, ...)
{
	va_list ap;
	va_start(ap, vb);
	char const *str;
	while (NULL != (str = va_arg(ap, char const *))) {
		size_t const len = strlen(str);
		if_fail (varbuf_append(vb, len, str)) break;
	}
	va_end(ap);
}

static void nulterm(struct varbuf *vb)
{
	vb->buf[vb->used] = '\0';
}

void varbuf_put(struct varbuf *vb, size_t size)
{
	if (vb->used + size > vb->actual) {
		size_t inc = vb->used + size - vb->actual;
		varbuf_make_room(vb, vb->actual + inc + (inc + 1)/2);	// will make room for one more byte
		on_error return;
	}
	vb->used += size;
	nulterm(vb);
}

void varbuf_chop(struct varbuf *vb, size_t size)
{
	assert(size < vb->used);
	vb->used -= size;
	nulterm(vb);
}

void varbuf_cut(struct varbuf *vb, char const *new_end)
{
	assert(new_end >= vb->buf && new_end <= vb->buf+vb->used);
	vb->used = new_end - vb->buf;
	nulterm(vb);
}

void varbuf_clean(struct varbuf *vb)
{
	vb->used = 0;
	nulterm(vb);
	// TODO: realloc buf toward initial guess ?
}

void varbuf_read_line(struct varbuf *vb, int fd, size_t maxlen, char **new)
{
	debug("varbuf_read_line(vb=%p, fd=%d)", vb, fd);
	on_error return;
	size_t prev_used = vb->used;
	if (new) *new = vb->buf + vb->used;	// new line will override this nul char
	bool was_CR = false;
	while (!is_error() && vb->used - prev_used < maxlen) {
		int8_t byte;
		ssize_t ret = pth_read(fd, &byte, 1);	// "If in doubt, use brute force"
		if (ret < 0) {
			if (errno != EINTR && errno != EAGAIN) error_push(errno, "Cannot pth_read");
		} else if (ret == 0) {
			error_push(ENOENT, "End of file");
		} else {
			assert(ret == 1);
			if (byte == '\r') {
				was_CR = true;
			} else {
				static char const cr = '\r';
				if (byte != '\n' && was_CR) varbuf_append(vb, 1, &cr);	// \r followed by anthing but \n are passed
				was_CR = false;
				if (! is_error()) varbuf_append(vb, 1, &byte);
				if (byte == '\n') break;
			}
		}
	}
}

void varbuf_write(struct varbuf const *vb, int fd)
{
	Write(fd, vb->buf, vb->used);
}

void varbuf_ctor_from_file(struct varbuf *vb, char const *filename)
{
	debug("make varbuf from '%s'", filename);
	int fd = open(filename, O_RDONLY);
	if (fd < 0) with_error(errno, "open(%s)", filename) return;
	off_t size = filesize(fd);
	if_succeed (varbuf_ctor(vb, size, false)) {
		if_succeed (varbuf_put(vb, size)) {
			Read(vb->buf, fd, size);
		}
	}
	(void)close(fd);
}
