#include <string.h>
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

void varbuf_makeroom(struct varbuf *vb, size_t new_size)
{
	if (vb->actual >= new_size) return;
	void *new_buf = realloc(vb->buf, new_size);
	if (! new_buf) THROW(exception_oom(new_size));
	if (new_buf != vb->buf && !vb->relocatable) {
		free(new_buf);
		THROW(exception_oom(new_size));
	}
	vb->buf = new_buf;
	vb->actual = new_size;
}

void varbuf_append(struct varbuf *vb, size_t size, void *buf)
{
	if (vb->used + size > vb->actual) {
		size_t inc = vb->used + size - vb->actual;
		varbuf_makeroom(vb, vb->actual + inc + (inc + 1)/2);
	}
	memcpy(vb->buf+vb->used, buf, size);
	vb->used += size;
}

int varbuf_gets(struct varbuf *vb, int fd)
{
	// TODO
	return -1;
}
