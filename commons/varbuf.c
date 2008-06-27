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
	size_t pos = vb->used;
	int err = varbuf_put(vb, size);
	if (! err) memcpy(vb->buf + pos, buf, size);
	return err;
}

int varbuf_put(struct varbuf *vb, size_t size)
{
	int err = 0;
	if (vb->used + size > vb->actual) {
		size_t inc = vb->used + size - vb->actual;
		if (0 != (err = varbuf_make_room(vb, vb->actual + inc + (inc + 1)/2))) return err;
	}
	vb->used += size;
	return err;
}

void varbuf_clean(struct varbuf *vb)
{
	vb->used = 0;
	// TODO: realloc buf toward initial guess ?
}

static int stringifies(struct varbuf *vb)
{
	if (! vb->used || vb->buf[vb->used-1] != '\0') {	// if this is a new varbuf, start with an empty string
		int err;
		if (0 != (err = varbuf_append(vb, 1, ""))) return err;
	}
	return 0;
}

ssize_t varbuf_read_line(struct varbuf *vb, int fd, size_t maxlen, char **new)
{
	int err = 0;
	if (0 != (err = stringifies(vb))) return err;
	vb->used--;	// chop nul char
	size_t prev_used = vb->used;
	if (new) *new = vb->buf + vb->used;	// new line will override this nul char
	while (vb->used - prev_used < maxlen) {
		int8_t byte;
		ssize_t ret = pth_read(fd, &byte, 1);	// "If in doubt, use brute force"
		if (ret < 0) {
			if (errno == EINTR) continue;
			err = -errno;
			break;
		} else if (ret == 0) {
			err = 0;
			break;
		}
		err ++;	// count read bytes
		assert(ret == 1);
		if (byte != '\r') varbuf_append(vb, 1, &byte);	// we just ignore them
		if (byte == '\n') break;
	}
	stringifies(vb);
	return err;
}

off_t varbuf_read_line_off(struct varbuf *vb, int fd, size_t maxlen, off_t offset, char **new)
{
	int err = 0;
	if (0 != (err = stringifies(vb))) return err;
	vb->used--;	// chop nul char
	size_t prev_used = vb->used;
	if (new) *new = vb->buf + vb->used;	// new line will override this nul char
	while (vb->used - prev_used < maxlen) {
		int8_t byte;
		ssize_t ret = pth_pread(fd, &byte, 1, offset);
		if (ret < 0) {
			if (errno == EINTR) continue;
			offset = -errno;
			break;
		} else if (ret == 0) break;
		assert(ret == 1);
		offset += 1;
		if (byte != '\r') varbuf_append(vb, 1, &byte);
		if (byte == '\n') break;
	}
	stringifies(vb);
	return offset;
}

