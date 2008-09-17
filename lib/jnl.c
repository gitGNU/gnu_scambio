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
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pth.h>
#include "scambio.h"
#include "jnl.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "misc.h"

/*
 * Data Definitions
 */

#define JNL_FNAME_FORMAT "%020"PRIversion".idx"
#define JNL_FNAME_LEN 24

static unsigned max_jnl_size;

struct index_entry {
	off_t offset;
};

/*
 * Init
 */

void jnl_begin(void)
{
	conf_set_default_int("MDIR_MAX_JNL_SIZE", 2000);
	on_error return;
	max_jnl_size = conf_get_int("MDIR_MAX_JNL_SIZE");
}

void jnl_end(void)
{
}

/*
 * New/Del
 */

static off_t filesize(int fd)
{
	off_t size = lseek(fd, 0, SEEK_END);
	if ((off_t)-1 == size) error_push(errno, "lseek");
	return size;
}

static unsigned fetch_nb_patches(int fd)
{
	off_t size = filesize(fd);
	on_error return 0;
	if (0 != size % sizeof(struct index_entry)) with_error(0, "Bad index file size : %lu", (unsigned long)size) return 0;
	return size / sizeof(struct index_entry);
}

static mdir_version parse_version(char const *filename)
{
	char *end;
	mdir_version version = strtoull(filename, &end, 10);
	if (0 != strcmp(end, ".idx")) with_error(0, "'%s' is not a journal file", filename) return 0;
	return version;
}
static void jnl_ctor(struct jnl *jnl, struct mdir *mdir, char const *filename)
{
	// Check filename
	jnl->version = parse_version(filename);
	on_error return;
	// Open both index and log files
	char path[PATH_MAX];
	int len = snprintf(path, sizeof(path), ".%s/%s", mdir->path, filename);
	jnl->idx_fd = open(path+1, O_RDWR|O_APPEND|O_CREAT, 0660);
	if (jnl->idx_fd == -1) with_error(errno, "open '%s'", path+1) return;
	memcpy(path + len - 3, "log", 4);	// change extention
	jnl->patch_fd = open(path, O_RDWR|O_APPEND|O_CREAT, 0660);
	if (jnl->patch_fd == -1) {
		error_push(errno, "open '%s'", path);
		goto q0;
	}
	// get sizes
	jnl->nb_patches = fetch_nb_patches(jnl->idx_fd);
	on_error goto q1;
	// All is OK, insert it as a journal
	if (STAILQ_EMPTY(&mdir->jnls)) {
		STAILQ_INSERT_HEAD(&mdir->jnls, jnl, entry);
	} else {	// insert in version order
		struct jnl *j, *prev = NULL;
		STAILQ_FOREACH(j, &mdir->jnls, entry) {
			if (j->version > jnl->version) break;
			if (j->version >= jnl->version || j->version + j->nb_patches > jnl->version) with_error(0, "Bad journals in '%s'", mdir->path) goto q1;
			prev = j;
		}
		if (prev) {
			STAILQ_INSERT_AFTER(&mdir->jnls, prev, jnl, entry);
		} else {
			STAILQ_INSERT_HEAD(&mdir->jnls, jnl, entry);
		}
	}
	jnl->mdir = mdir;
	return;
q1:
	(void)close(jnl->patch_fd);
q0:
	(void)close(jnl->idx_fd);
}

struct jnl *jnl_new(struct mdir *mdir, char const *filename)
{
	struct jnl *jnl = malloc(sizeof(*jnl));
	if (! jnl) with_error(ENOMEM, "malloc jnl %s", filename) return NULL;
	jnl_ctor(jnl, mdir, filename);
	on_error {
		free(jnl);
		return NULL;
	}
	return jnl;
}

static void jnl_dtor(struct jnl *jnl)
{
	STAILQ_REMOVE(&jnl->mdir->jnls, jnl, jnl, entry);
	if (jnl->idx_fd >= 0) (void)close(jnl->idx_fd);
	jnl->idx_fd = -1;
	if (jnl->patch_fd >= 0) (void)close(jnl->patch_fd);
	jnl->patch_fd = -1;
}

void jnl_del(struct jnl *jnl)
{
	jnl_dtor(jnl);
	free(jnl);
}

struct jnl *jnl_new_empty(struct mdir *mdir, mdir_version start_version)
{
	char filename[JNL_FNAME_LEN + 1];
	snprintf(filename, sizeof(filename), JNL_FNAME_FORMAT, start_version);
	return jnl_new(mdir, filename);
}

/*
 * Patch
 */

void jnl_patch(struct jnl *jnl, enum mdir_action action, struct header *header)
{
	struct index_entry ie;
	ie.offset = filesize(jnl->patch_fd);
	on_error return;
	// Write the index
	Write(jnl->idx_fd, &ie, sizeof(ie));
	on_error return;	// FIXME: on short writes, truncate
	// Then the patch command
	Write(jnl->patch_fd, action == MDIR_ADD ? "+\n":"-\n", 2);
	on_error return;
	struct header *alt_header = NULL;
	char const *key;
	if (action == MDIR_REM && NULL != (key = header_search(header, SCAMBIO_KEY_FIELD))) {
		// We are allowed to replace the content by the key only
		alt_header = header_new();
		on_error {
			alt_header = NULL;
		} else {
			header_add_field(alt_header, SCAMBIO_KEY_FIELD, key);
			on_error {
				alt_header = NULL;
				header_del(alt_header);
			}
		}
		error_clear();
	}
	header_write(alt_header ? alt_header:header, jnl->patch_fd);
	if (alt_header) header_del(alt_header);
	unless_error jnl->nb_patches ++;
	// FIXME: triggers all listeners that something was appended
}

/*
 * Read
 */

// read index file to find patch offset
static off_t jnl_offset_size(struct jnl *jnl, unsigned index, size_t *size)
{
	struct index_entry ie[2];
	if (index < jnl->nb_patches - 1) {	// can read 2 entires
		Read(ie, jnl->idx_fd, index*sizeof(*ie), 2*sizeof(*ie));
		on_error return 0;
	} else {	// at end of index file, will need log file size
		Read(ie, jnl->idx_fd, index*sizeof(*ie), 1*sizeof(*ie));
		on_error return 0;
		ie[1].offset = filesize(jnl->patch_fd);
		on_error return 0;
	}
	assert(ie[1].offset > ie[0].offset);
	*size = ie[1].offset - ie[0].offset;
	return ie[0].offset;
}

struct header *jnl_read(struct jnl *jnl, mdir_version version, enum mdir_action *action)
{
	struct header *header = NULL;
	size_t size;
	off_t offset = jnl_offset_size(jnl, version - jnl->version, &size);
	on_error return NULL;
	// Read the whole patch
	char *buf = malloc(size);
	if (! buf) with_error(ENOMEM, "malloc %zu bytes", size) return NULL;
	do {
		Read(buf, jnl->patch_fd, offset, size);
		on_error break;
		if (buf[1] != '\n' || buf[size-2] != '\n' || buf [size-1] != '\n') with_error(0, "Invalid patch @%lu", (unsigned long)offset) break;
		if (buf[0] == '+') {
			*action = MDIR_ADD;
		} else if (buf[0] == '-') {
			*action = MDIR_REM;
		} else {
			with_error(0, "Unknown action @%lu", (unsigned long)offset) break;
		}
		// read header
		header = header_new();
		on_error break;
		header_parse(header, buf);
	} while (0);
	free(buf);
	return header;
}

/*
 * Utils
 */

bool jnl_too_big(struct jnl *jnl)
{
	return jnl->nb_patches >= max_jnl_size;
}
