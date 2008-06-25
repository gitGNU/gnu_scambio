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

extern inline int varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable);
extern inline void varbuf_dtor(struct varbuf *vb);

int varbuf_make_room(struct varbuf *vb, size_t new_size)
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
		if (0 != (err = varbuf_make_room(vb, vb->actual + inc + (inc + 1)/2))) return err;
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
	int err = 0;
	varbuf_clean(vb);
	while (vb->used < maxlen) {
		int8_t byte;
		ssize_t ret = pth_read(fd, &byte, 1);	// "If in doubt, use brute force"
		if (ret < 0) {
			if (errno == EINTR) continue;
			err = -errno;
			break;
		} else if (ret == 0) {
			break;
		}
		assert(ret == 1);
		varbuf_append(vb, 1, &byte);
		if (byte == '\n') break;
	}
	varbuf_append(vb, 1, "");	// always null-term the string
	return err;
}

off_t varbuf_read_line_off(struct varbuf *vb, int fd, size_t maxlen, off_t offset)
{
	varbuf_clean(vb);
	while (vb->used < maxlen) {
		int8_t byte;
		ssize_t ret = pth_pread(fd, &byte, 1, offset);	// "If in doubt, use brute force"
		if (ret < 0) {
			if (errno == EINTR) continue;
			offset = -errno;
			break;
		} else if (ret == 0) {
			break;
		}
		assert(ret == 1);
		offset += 1;
		varbuf_append(vb, 1, &byte);
		if (byte == '\n') break;
	}
	varbuf_append(vb, 1, "");	// always null-term the string
	return offset;
}

