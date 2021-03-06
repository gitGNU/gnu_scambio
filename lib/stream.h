/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef STREAM_H_081024
#define STREAM_H_081024

#include <limits.h>
#include <time.h>
#include <stdbool.h>
#include "scambio/queue.h"
#include "scambio/channel.h"

extern char const *chn_files_root;
extern unsigned chn_files_root_len;
extern char chn_putdir[PATH_MAX];
extern unsigned chn_putdir_len;

/* We use the abstraction of a stream, which have a name (the name used
 * as resource locator) to which we can append data or read from a
 * cursor (per TX). Each stream may have at most one writer, but can have
 * many readers.
 */
struct stream {
	LIST_ENTRY(stream) entry;	// in the list of all loaded streams
	LIST_HEAD(readers, chn_tx) readers;
	bool has_writer;
	int count;	// each reader/writer count as 1
	int fd;	// may be -1 if not mapped to a file
	time_t last_used;	// usefull for RT streams
	char path[PATH_MAX];
	pth_t pth;	// thread that push file onto reading TXs
};

void stream_begin(void);
void stream_end(void);
struct stream *stream_lookup(char const *name, bool rt);	// will find an existing stream or load a new file backed stream.
struct stream *stream_new(char const *name, bool rt);	// create a new stream for the given resource
void stream_del(struct stream *stream);
static inline struct stream *stream_ref(struct stream *stream)
{
	stream->count++;
	return stream;
}
static inline void stream_unref(struct stream *stream)
{
	if (--stream->count <= 0) {
		stream_del(stream);
	}
}

/* Will fail if there is already one writer */
void stream_add_writer(struct stream *stream);
void stream_remove_writer(struct stream *stream);
void stream_write(struct stream *stream, off_t offset, size_t size, struct chn_box *box, bool eof);

void stream_add_reader(struct stream *stream, struct chn_tx *tx);
void stream_remove_reader(struct stream *stream, struct chn_tx *tx);

#endif
