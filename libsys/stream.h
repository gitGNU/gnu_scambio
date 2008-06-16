/* A raw stream is a way to read/write data sequentialy.
 * Its used to abstract files, stream sockets, or anything
 * else we can think of that is not already accessible with
 * a file descriptor.
 *
 * Uses an asynch API as an EDP module.
 *
 * API: open, read, write, close.
 * callbacks : opened, read, written, closed.
 *
 * First create an actual stream using one of the derived type, then use these functions.
 */

#ifndef STREAM_H_080616
#define STREAM_H_080616

// err = 0 = OK, all other are stream type dependant
struct stream_status {
	struct stream *stream;
	int err;
	size_t asked;	// undef for open and close results
	size_t performed;	// idem
};

struct stream_ops {
	struct void (*open)(struct stream *, edp_callback *cb, void *data);
	// For both read and write, data must be available until cb is called (goes without saying of course)
	struct void (*write)(struct stream *, size_t size, void *data, edp_callback  *cb, void *data);
	struct void (*read)(struct stream *, size_t size, void *data, edp_callback *cb, void *data);
	struct void (*close)(struct stream *, edp_callback *cb, void *data);
};

struct stream {
	struct stream_ops const *ops;
};

static inline int stream_ctor(struct stream *stream, struct stream_ops const *ops)
{
	stream->ops = ops;
	return 0;
}

static inline void stream_dtor(struct stream *stream)
{
}

#endif
