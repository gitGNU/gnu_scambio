#ifndef MISC_H_080625
#define MISC_H_080625

#include <stddef.h>	// size_t (? FIXME)
int Write(int fd, void const *buf, size_t len);
int Read(void *buf, int fd, off_t offset, size_t len);

#endif
