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
#ifndef VARBUF_H_080617
#define VARBUF_H_080617
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include "scambio.h"

/* Straigforward variable sized buffer data type.
 * For all buffers which size are unknown.
 * Also, as a commodity, all data put in there will be stored with an
 * additional nul trailing byte so that content may be used as a C string
 * (this nul byte is not counted in the buffer size).
 */
struct varbuf {
	size_t used;	// Length of data that was put/appended to the buffer.
	size_t actual;	// Actuel length of malloced memory (used < actual).
	bool relocatable;	// Allow the buffer to grows beyond initial size request (ie buf may move!)
	char *buf;	// Mallocated data. Content will always by nul terminated.
};

/* Constructor.
 * If relocatable, init_size is just a guess, but buf pointer may change randomly.
 * Throws ENOMEN.
 */
static inline void varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable)
{
	vb->used = 0;
	vb->actual = init_size+1;	// we want to be able to add a nul byte
	vb->relocatable = relocatable;
	vb->buf = malloc(vb->actual);
//	debug("ctor @%p, size=%zu, buf=%p", vb, init_size, vb->buf);
	if (! vb->buf) with_error(ENOMEM, "Cannot malloc for varbuf") return;
}

/* Destructor.
 * Throws no error.
 */
static inline void varbuf_dtor(struct varbuf *vb)
{
//	debug("deleting @%p", vb);
	free(vb->buf);
}

/* Once build, you may want to be given the responsability for the content.
 * You are then required to free it at some point.
 * Throws no error.
 */
static inline void *varbuf_unbox(struct varbuf *vb)
{
	// unboxing mean that the caller wants the buffer, but not the varbuf
	// we merely returns here, since we have nothing to destruct in the varbuf.
//	debug("unboxing @%p", vb);
	return vb->buf;
}

/* Append new data at the end of content.
 * Throws ENOMEM.
 */
void varbuf_append(struct varbuf *, size_t size, void const *buf);

/* Append a set a strings at the end of content.
 * All parameters must be char*, terminated by NULL.
 * Throws ENOMEM.
 */
void varbuf_append_strs(struct varbuf *, ...)
#ifdef __GNUC__
	__attribute__ ((sentinel))
#endif
;

/* Grows the size of a varbuf. The new content at the end is undefined.
 * The nul byte will still be present at the end.
 * Throws ENOMEM.
 */
void varbuf_put(struct varbuf *, size_t size);

/* Reduce the size of a varbuf, for deleting the end of the content.
 * The nul byte will still be present at the end.
 * Throws no error.
 */
void varbuf_chop(struct varbuf *, size_t size);

/* Reduce the size of a varbuf, by changing the new end.
 * Ideal with the pointer returned by varbuf_read_line();
 * Throws no error.
 */
void varbuf_cut(struct varbuf *, char const *new_end);

/* Reads one more line of text from file into varbuf (converting CRLF into LF).
 * Throws ENOENT on EOF and many other errors on various occasions.
 */
void varbuf_read_line(struct varbuf *, int fd, size_t maxlen, char **new);

/* Writes content of the varbuf to the given file.
 * Throws many kind of errors.
 */
void varbuf_write(struct varbuf const *vb, int fd);

/* Empty the varbuf.
 * Throws no error.
 */
void varbuf_clean(struct varbuf *);

/* Construct a fixed-length varbuf with the content of the given file.
 */
void varbuf_ctor_from_file(struct varbuf *vb, char const *filename);

#endif
