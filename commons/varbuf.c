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
#include <pth.h>
#include "varbuf.h"

/*
 * Data Definitions
 */

/*
 * Private Functions
 */

/*
 * Public Functions
 */

extern inline void varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable);
extern inline void varbuf_dtor(struct varbuf *vb);

void varbuf_make_room(struct varbuf *vb, size_t new_size)
{
	if (vb->actual >= new_size) return;
	void *new_buf = realloc(vb->buf, new_size);
	if (! new_buf) with_error(ENOMEM, "Cannot realloc varbug to %zu bytes", new_size) return;
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
	varbuf_put(vb, size);
	on_error return;
	memcpy(vb->buf + pos, buf, size);
}

void varbuf_put(struct varbuf *vb, size_t size)
{
	if (vb->used + size > vb->actual) {
		size_t inc = vb->used + size - vb->actual;
		varbuf_make_room(vb, vb->actual + inc + (inc + 1)/2);
		on_error return;
	}
	vb->used += size;
}

void varbuf_chop(struct varbuf *vb, size_t size)
{
	if (vb->used < size) with_error(EINVAL, "Chop would extend") return;
	vb->used -= size;
}

void varbuf_clean(struct varbuf *vb)
{
	vb->used = 0;
	// TODO: realloc buf toward initial guess ?
}

void varbuf_stringifies(struct varbuf *vb)
{
	if (! vb->used || vb->buf[vb->used-1] != '\0') {	// if this is a new varbuf, start with an empty string
		varbuf_append(vb, 1, "");
	}
}

void varbuf_read_line(struct varbuf *vb, int fd, size_t maxlen, char **new)
{
	debug("varbuf_read_line(vb=%p, fd=%d)", vb, fd);
	varbuf_stringifies(vb);
	on_error return;
	vb->used--;	// chop nul char
	size_t prev_used = vb->used;
	if (new) *new = vb->buf + vb->used;	// new line will override this nul char
	bool was_CR = false;
	while (!is_error() && vb->used - prev_used < maxlen) {
		int8_t byte;
		ssize_t ret = pth_read(fd, &byte, 1);	// "If in doubt, use brute force"
		if (ret < 0) {
			if (errno != EINTR) error_push(errno, "Cannot pth_read");
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
	varbuf_stringifies(vb);
}

