/* Variable sized buffer data type.
 */
#ifndef VARBUF_H_080617
#define VARBUF_H_080617
#include <stddef.h>
#include <stdbool.h>
#include "exception.h"

struct varbuf {
	size_t used, actual;	// used <= actual !
	bool relocatable;
	void *buf;
};

#include <stdlib.h>
static inline void varbuf_ctor(struct varbuf *vb, size_t init_size, bool relocatable)
{
	vb->used = 0;
	vb->actual = init_size;
	vb->relocatable = relocatable;
	vb->buf = malloc_or_throw(init_size);
}

static inline void varbuf_dtor(struct varbuf *vb)
{
	free(vb->buf);
}

void varbuf_makeroom(struct varbuf *vb, size_t new_size);
void varbuf_append(struct varbuf *vb, size_t size, void *buf);

int varbuf_gets(struct varbuf *vb, int fd);

#endif
