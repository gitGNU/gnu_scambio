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
char chn_putdir[PATH_MAX];
unsigned chn_putdir_len;

/*
 * Helpers
 */

bool resource_is_ref(char const *name)
{
	debug("Is name '%s' a ref ?", name);
	return 0 == strncmp("refs/", name, 5);
}

static bool path_is_ref(char const *path)
{
	return resource_is_ref(path + chn_files_root_len+1);
}

static bool stream_is_ref(struct stream const *stream)
{
	return stream->fd != -1 && path_is_ref(stream->path);
}

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
			pth_cancel_point();
#			define STREAM_READ_BLOCK 10000
			bool eof = false;
			off_t totsize = filesize(stream->fd);
			size_t size = STREAM_READ_BLOCK;
			if ((off_t)size + tx->end_offset >= totsize) {
				size = totsize - tx->end_offset;
				eof = true;
			}
			debug("write %zu bytes / %u", size, (unsigned)totsize);
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

static void stream_ctor(struct stream *stream, char const *name, bool rt, bool can_create)
{
	debug("stream @%p with name=%s, rt=%c", stream, name, rt ? 'y':'n');
	LIST_INIT(&stream->readers);
	stream->has_writer = false;
	stream->count = 1;	// the one who asks
	snprintf(stream->path, sizeof(stream->path), "%s/%s", chn_files_root, name);
	stream->last_used = time(NULL);
	bool is_ref = resource_is_ref(name);
	stream->was_created = false;	// useless for RT, BTW
	if (rt) {
		if (is_ref) with_error(0, "RT streams cannot use references") return;
		stream->fd = -1;
		stream->pth = NULL;
	} else {
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", chn_files_root, name);
		stream->fd = open(path, O_RDWR);
		if (stream->fd < 0 && errno == ENOENT && can_create) {
			if_fail (Mkdir_for_file(path)) return;
			stream->fd = open(path, O_RDWR | O_CREAT, 0644);	// we can write straight into refs
			stream->was_created = true;
		}
		if (stream->fd < 0) with_error(errno, "open(%s)", path) return;
		// FIXME: in stream_add_reader if stream->fd != -1 ?
		stream->pth = pth_spawn(PTH_ATTR_DEFAULT, stream_push, stream);
		if (! stream->pth) {
			(void)close(stream->fd);
			with_error(0, "pth_spawn(stream_push)") return;
		}
	}
	debug("stream will use fd = %d, of size %lu", stream->fd, (unsigned long)filesize(stream->fd));
	LIST_INSERT_HEAD(&streams, stream, entry);
}

struct stream *stream_new(char const *name, bool rt, bool can_create)
{
	struct stream *stream = malloc(sizeof(*stream));
	if (! stream) with_error(ENOMEM, "malloc(stream)") return NULL;
	if_fail (stream_ctor(stream, name, rt, can_create)) {
		free(stream);
		stream = NULL;
	}
	return stream;
}

static void stream_dtor(struct stream *stream)
{
	debug("stream @%p", stream);
	LIST_REMOVE(stream, entry);
	assert(LIST_EMPTY(&stream->readers));
	assert(! stream->has_writer);
	assert(stream->count <= 0);
	if (stream->pth) {
		debug("cancelling stream thread");
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
	if_fail(conf_set_default_str("SC_FILES_DIR", "/var/lib/scambio/files")) return;
	chn_files_root = conf_get_str("SC_FILES_DIR");
	chn_files_root_len = strlen(chn_files_root);
	Mkdir(chn_files_root);
	chn_putdir_len = snprintf(chn_putdir, sizeof(chn_putdir), "%s/.put", chn_files_root);
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

struct stream *stream_lookup(char const *name, bool can_create)
{
	debug("name=%s", name);
	if (name[0] == '.') with_error(0, "Resource not allowed to start with '.'") return NULL;
	struct stream *stream;
	LIST_FOREACH(stream, &streams, entry) {
		if (0 == strcmp(stream->path + chn_files_root_len + 1, name)) return stream_ref(stream);
	}
	return stream_new(name, false, can_create);
}

/*
 * Write
 *
 * Writing to a stream is writting to all its reading TXs, and optionaly to its backup file.
 */

void stream_write(struct stream *stream, off_t offset, size_t size, struct chn_box *box, bool eof)
{
	assert(stream->has_writer);
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

void stream_add_writer(struct stream *stream)
{
	debug("stream@%p", stream);
	if (stream->has_writer) with_error(0, "a stream cannot have more than one writer") return;
	if (!stream->was_created && stream_is_ref(stream)) with_error(0, "Cannot write to a stream opened by ref") return;
	stream->last_used = time(NULL);
	stream->has_writer = true;
	stream_ref(stream);
}

void stream_remove_writer(struct stream *stream)
{
	debug("stream@%p", stream);
	assert(stream->has_writer);
	stream->has_writer = false;
	stream_unref(stream);
	if (stream->fd != -1 && !stream_is_ref(stream)) {	// Make/Renew the SHA ref
		char digest[CHN_REF_LEN];
		if_fail (chn_ref_from_file(stream->path, digest)) return;
		char ref[PATH_MAX]; 
		snprintf(ref, sizeof(ref), "%s/%s", chn_files_root, digest);
		if (0 != unlink(ref)) {	// for the case where this ref already pointed to another file
			if (errno != ENOENT) with_error(errno, "unlink(%s)", ref) return;
		}
		if_fail (Mkdir_for_file(ref)) return;
		/* Now two cases : the resource may be a new one, in which case the file is a regular file,
		 * and we have to create a ref, remove the file and make it a symlink to this ref, or
		 * the file is already a symlink, and we then have to remove the former ref and relink to the
		 * new one.
		 */
		char old_link[PATH_MAX];
		ssize_t old_link_len  = readlink(stream->path, old_link, sizeof(old_link));
		if (old_link_len > 0) {	// already a symlink : rename the previous ref (content is already updated)
			old_link[old_link_len] = '\0';
			if (0 != rename(old_link, ref)) with_error(errno, "rename(%s, %s)", old_link, ref) return;
			if (0 != unlink(stream->path)) with_error(errno, "unlink(%s)", stream->path) return;
		} else {
			if (errno != EINVAL) with_error(errno, "readlink(%s)", stream->path) return;
			// not a symlink : rename the path to the ref file
			if (0 != rename(stream->path, ref)) with_error(errno, "rename(%s, %s)", stream->path, ref) return;
		}
		// Make the resource name a symlink to the ref
		if (0 != symlink(ref, stream->path)) with_error(errno, "symlink(%s, %s)", ref, stream->path) return;
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
	stream_ref(stream);
}

void stream_remove_reader(struct stream *stream, struct chn_tx *tx)
{
	debug("stream@%p, reader@%p", stream, tx);
	LIST_REMOVE(tx, reader_entry);
	stream_unref(stream);;
}

