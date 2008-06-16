#include "stream.h"

extern inline int stream_ctor(struct stream *stream, struct stream_ops const *ops);
extern inline void stream_dtor(struct stream *stream);

