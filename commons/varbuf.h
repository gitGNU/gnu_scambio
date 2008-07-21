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
	debug("varbuf_dtor(vb=%p)", vb);
	free(vb->buf);
}

int varbuf_make_room(struct varbuf *, size_t new_size);
int varbuf_append(struct varbuf *, size_t size, void const *buf);
int varbuf_put(struct varbuf *, size_t size);
// reads one more line of text into varbuf (converting CRLF into LF)
// returns 1 on EOF, <0 on error
int varbuf_read_line(struct varbuf *, int fd, size_t maxlen, char **new);
//off_t varbuf_read_line_off(struct varbuf *, int fd, size_t maxlen, off_t, char **new);
void varbuf_clean(struct varbuf *);

#endif
