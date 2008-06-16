#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "filestream.h"

/*
 * Definitions
 */

struct filestream {
	struct stream stream;
	int fd;
	unsigned readable:1, writeable:1;
};

/*
 * Open
 */

static void filestream_open(struct stream *stream, edp_callback *cb, void *data)
{
	struct filestream *fs = DOWNCAST(stream, stream, filestream);
	struct stream_status *status = edp_push_event(cb, sizeof(*status), data);
	status->stream = stream;
	status->err = fs->fd == -1 ? -1:0;
}

static void filestream_close(struct stream *stream, edp_callback *cb, void *data)
{
	struct filestream *fs = DOWNCAST(stream, stream, filestream);
	struct stream_status *status = edp_push_event(cb, sizeof(*status), data);
	status->stream = stream;
	status->err = -1;
	if (fs->fd != -1) {
		if (0 != close(fs->fd)) {
			printl(LOG_ERR, __FUNCTION__": Cannot close file : %s", strerror(errno));
		} else {
			status->err = 0;
		}
	}
}

/*
 * Creating a filestream
 */

static int filestream_ctor(struct filestream *fs, char const *path, bool will_read, bool will_write)
{
	struct stream_ops const my_ops = {
		.open = filestream_open,
		.read = filestream_read,
		.write = filestream_write,
		.close = filestream_close,
	};
	if (0 != stream_ctor(&fs->stream, &my_ops)) return -1;
	fs->readable = will_read;
	fs->writeable = will_write;
	int flags = 0;
	if (will_write) {
		flags |= O_CREAT;
		if (will_read) {
			flags |= O_RDWR;
		} else {
			flags |= O_WRONLY;
		}
	} else {
		if (will_read) flags |= O_RDONLY;
	}
	fs->fd = open(path, flags);
	if (fs->fd == -1) {
		printl(LOG_ERR, __FUNCTION__": Cannot open file '%s' : %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

struct filestream *filestream_new(char const *path, bool will_read, bool will_write)
{
	struct filestream *fs = malloc(sizeof(*fs));
	if (! fs) return NULL;
	if (0 != filestream_ctor(fs, path, will_read, will_write)) {
		free(fs);
		return NULL;
	}
	return &fs->stream;
}

/*
 * Deleting a filestream
 */

void filestream_del(struct filestream *);

/*
 * Conversion
 */

extern inline struct stream *filestream2stream(struct filestream *filestream);
