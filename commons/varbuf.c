#include <string.h>
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

extern inline int varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable);
extern inline void varbuf_dtor(struct varbuf *vb);

int varbuf_makeroom(struct varbuf *vb, size_t new_size)
{
	if (vb->actual >= new_size) return 0;
	void *new_buf = realloc(vb->buf, new_size);
	if (! new_buf) return -ENOMEM;
	if (new_buf != vb->buf && !vb->relocatable) {
		free(new_buf);
		return -ENOMEM;
	}
	vb->buf = new_buf;
	vb->actual = new_size;
	return 0;
}

int varbuf_append(struct varbuf *vb, size_t size, void *buf)
{
	int err;
	if (vb->used + size > vb->actual) {
		size_t inc = vb->used + size - vb->actual;
		if (0 != (err = varbuf_makeroom(vb, vb->actual + inc + (inc + 1)/2))) return err;
	}
	memcpy(vb->buf+vb->used, buf, size);
	vb->used += size;
	return 0;
}

void varbuf_clean(struct varbuf *vb)
{
	vb->used = 0;
	// TODO: realloc buf toward initial guess ?
}

ssize_t varbuf_read_line(struct varbuf *vb, int fd, size_t maxlen)
{
	ssize_t ret = pth_read(fd, vb->buf+vb->used, vb->actual-vb->used);
	// TODO
	return ret;
}

