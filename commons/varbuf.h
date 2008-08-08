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
/* Variable sized buffer data type.
 */
#ifndef VARBUF_H_080617
#define VARBUF_H_080617
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include "scambio.h"

struct varbuf {
	size_t used, actual;	// used <= actual !
	bool relocatable;
	char *buf;
};

static inline int varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable)
{
	vb->used = 0;
	vb->actual = init_size;
	vb->relocatable = relocatable;
	vb->buf = malloc(init_size);
	debug("varbuf_ctor(vb=%p, size=%zu)->@%p", vb, init_size, vb->buf);
	if (! vb->buf) return -ENOMEM;
	return 0;
}

static inline void varbuf_dtor(struct varbuf *vb)
{
	free(vb->buf);
}

static inline void *varbuf_unbox(struct varbuf *vb)
{
	// unboxing mean that the caller wants the buffer, but not the varbuf
	// we merely returns here, since we have nothing to destruct in the varbuf.
	return vb->buf;
}

int varbuf_make_room(struct varbuf *, size_t new_size);
int varbuf_append(struct varbuf *, size_t size, void const *buf);
int varbuf_put(struct varbuf *, size_t size);
int varbuf_chop(struct varbuf *, size_t size);
// reads one more line of text into varbuf (converting CRLF into LF)
// returns 1 on EOF, <0 on error
int varbuf_read_line(struct varbuf *, int fd, size_t maxlen, char **new);
//off_t varbuf_read_line_off(struct varbuf *, int fd, size_t maxlen, off_t, char **new);
void varbuf_clean(struct varbuf *);
int varbuf_stringifies(struct varbuf *vb);

#endif
