#ifndef FILESTREAM_H_080616
#define FILESTREAM_H_080616

#include <stdbool.h>
#include "stream.h"

struct stream *filestream_new(char const *path, bool will_read, bool will_write);
void filestream_del(struct stream *);

#endif
