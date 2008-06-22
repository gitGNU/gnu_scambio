#ifndef MESSAGE_H_080622
#define MESSAGE_H_080622

#include "varbuf.h"
#include "header.h"

int read_url(struct varbuf *vb, int fd);
int read_header(struct varbuf *vb, int fd);
void insert_message(struct varbuf *vb, struct header *head);

#endif
