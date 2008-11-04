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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "scambio.h"
#include "misc.h"
#include "stream.h"

/*
 * Data Definitions
 */

static LIST_HEAD(streams, stream) streams;	// list of all loaded streams
char const *chn_files_root;
unsigned chn_files_root_len;

/*
 * Create/Delete
 */

static void *stream_push(void *arg)
{
	struct stream *stream = arg;
	struct chn_tx *tx;
	while (1) {
		while (LIST_EMPTY(&stream->readers)) pth_usleep(10000);	// FIXME
		debug("for each reader...");
		LIST_FOREACH(tx, &stream->readers, reader_entry) {
#			define STREAM_READ_BLOCK 10000
			bool eof = false;
			off_t totsize = filesize(stream->fd);
			size_t size = STREAM_READ_BLOCK;
			if ((off_t)size + tx->end_offset >= totsize) {
				size = totsize - tx->end_offset;
				eof = true;
			}
			debug("write %zu bytes / %u", size, (unsigned)totsize);
			if (! size) continue;
			struct chn_box *box;
			if_fail (box = chn_box_alloc(size)) return NULL;
			if_fail (ReadFrom(box->data, stream->fd, tx->end_offset, size)) return NULL;
			chn_tx_write(tx, size, box, eof);
			chn_box_unref(box);
			on_error return NULL;
		}
		pth_yield(NULL);
	}
	return NULL;
}

static void stream_ctor(struct stream *stream, char const *name, bool rt)
{
	LIST_INIT(&stream->readers);
	stream->writer = NULL;
	stream->count = 1;	// the one who asks
	snprintf(stream->name, sizeof(stream->name), "%s", name);
	stream->last_used = time(NULL);
	if (rt) {
		stream->fd = -1;
		stream->pth = NULL;
	} else {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", chn_files_root, name);
		stream->fd = open(path, O_RDWR);
		if (stream->fd < 0) with_error(errno, "open(%s)", path) return;
		// FIXME: in stream_add_reader if stream->fd != -1 ?
		stream->pth = pth_spawn(PTH_ATTR_DEFAULT, stream_push, stream);
		if (! stream->pth) {
			(void)close(stream->fd);
			with_error(0, "pth_spawn(stream_push)") return;
		}
	}
	LIST_INSERT_HEAD(&streams, stream, entry);
}

static struct stream *stream_new(char const *name, bool rt)
{
	struct stream *stream = malloc(sizeof(*stream));
	if (! stream) with_error(ENOMEM, "malloc(stream)") return NULL;
	if_fail (stream_ctor(stream, name, rt)) {
		free(stream);
		stream = NULL;
	}
	return stream;
}

static void stream_dtor(struct stream *stream)
{
	LIST_REMOVE(stream, entry);
	assert(LIST_EMPTY(&stream->readers));
	assert(! stream->writer);
	assert(stream->count <= 0);
	if (stream->pth) {
		pth_cancel(stream->pth);
		stream->pth = NULL;
	}
	if (stream->fd != -1) {
		(void)close(stream->fd);
		stream->fd = -1;
	}
}

void stream_del(struct stream *stream)
{
	stream_dtor(stream);
	free(stream);
}

extern inline struct stream *stream_ref(struct stream *stream);
extern inline void stream_unref(struct stream *stream);

/*
 * (De)Init
 */

void stream_begin(void)
{
	LIST_INIT(&streams);
	conf_set_default_str("SCAMBIO_FILES_DIR", "/tmp/files");
	chn_files_root = conf_get_str("SCAMBIO_FILES_DIR");
	chn_files_root_len = strlen(chn_files_root);
	Mkdir(chn_files_root);
}

void stream_end(void)
{
	struct stream *stream;
	// break references : delete all TXs first !
	while (NULL != (stream = LIST_FIRST(&streams))) stream_del(stream);
}

/*
 * Lookup
 */

struct stream *stream_lookup(char const *name)
{
	struct stream *stream;
	LIST_FOREACH(stream, &streams, entry) {
		if (0 == strcmp(stream->name, name)) return stream;
	}
	return stream_new(name, false);
}

struct stream *stream_new_rt(char const *name)
{
	return stream_new(name, true);
}

/*
 * Write
 *
 * Writing to a stream is writting to all its reading TXs, and optionaly to its backup file.
 */

void stream_write(struct stream *stream, off_t offset, size_t size, struct chn_box *box, bool eof)
{
	debug("Writing to stream which fd = %d", stream->fd);
	stream->last_used = time(NULL);
	if (stream->fd != -1) {	// append to file
		if_fail (WriteTo(stream->fd, offset, box->data, size)) return;
		if (eof && 0 != ftruncate(stream->fd, offset + size)) with_error(errno, "truncate stream") return;
	}
	struct chn_tx *tx;
	LIST_FOREACH(tx, &stream->readers, reader_entry) {
		assert(tx->stream == stream);
		chn_tx_write(tx, size, box, eof);
	}
}

/*
 * Reader thread
 */

void stream_add_reader(struct stream *stream, struct chn_tx *tx)
{
	debug("stream@%p, reader@%p", stream, tx);
	stream->last_used = time(NULL);
	if (LIST_EMPTY(&stream->readers)) {
		// we should start a reader thread (add its pth_tid to the struct stream)
	}
	LIST_INSERT_HEAD(&stream->readers, tx, reader_entry);
}

