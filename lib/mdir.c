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
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/header.h"
#include "scambio/mdir.h"
#include "varbuf.h"
#include "log.h"
#include "misc.h"
#include "digest.h"
#include "persist.h"
#include "jnl.h"

/*
 * Data Definitions
 *
 */

static size_t mdir_root_len;
static char const *mdir_root;
static char mdir_namespace;
static LIST_HEAD(mdirs, mdir) mdirs;
static struct persist dirid_seq;
struct mdir *(*mdir_alloc)(void);
void (*mdir_free)(struct mdir *);

/*
 * Default allocator
 */

static struct mdir *mdir_alloc_default(void)
{
	struct mdir *mdir = malloc(sizeof(*mdir));
	if (! mdir) error_push(ENOMEM, "malloc mdir");
	return mdir;
}

static void mdir_free_default(struct mdir *mdir)
{
	free(mdir);
}

/*
 * mdir creation
 */

// path must be mdir_root + "/" + id
static void mdir_ctor(struct mdir *mdir, char const *id, bool create)
{
	snprintf(mdir->path, sizeof(mdir->path), "%s/%s", mdir_root, id);
	if (create) {
		Mkdir(mdir->path);
		on_error return;
	}
	(void)pth_rwlock_init(&mdir->rwlock);	// FIXME: if this can schedule some other thread, we need to prevent addition of the same dir twice
	STAILQ_INIT(&mdir->jnls);
	// Find journals
	DIR *d = opendir(mdir->path);
	if (! d) with_error(errno, "opendir %s", mdir->path) return;
	struct dirent *dirent;
	// We may yield the CPU here, but then we must acquire write lock
	while (NULL != (dirent = readdir(d))) {
		if (is_jnl_file(dirent->d_name)) {
			jnl_new(mdir, dirent->d_name);
			on_error break;
		}
	}
	if (0 != closedir(d)) error_push(errno, "Cannot closedir %s", mdir->path);
	unless_error LIST_INSERT_HEAD(&mdirs, mdir, entry);
}

static struct mdir *mdir_new(char const *id, bool create)
{
	struct mdir *mdir = mdir_alloc();
	on_error return NULL;
	mdir_ctor(mdir, id, create);
	on_error {
		mdir_free(mdir);
		return NULL;
	}
	return mdir;
}

struct mdir *mdir_create(void)
{
	debug("create a new mdir");
	uint64_t id = (*(uint64_t *)dirid_seq.data)++;
	char id_str[1+20+1];
	snprintf(id_str, sizeof(id_str), "%c%"PRIu64, mdir_namespace, id);
	return mdir_new(id_str, true);
}

static void mdir_dtor(struct mdir *mdir)
{
	(void)pth_rwlock_acquire(&mdir->rwlock, PTH_RWLOCK_RW, FALSE, NULL);
	LIST_REMOVE(mdir, entry);
	struct jnl *jnl;
	while (NULL != (jnl = STAILQ_FIRST(&mdir->jnls))) {
		jnl_del(jnl);
	}
	pth_rwlock_release(&mdir->rwlock);
}

static void mdir_del(struct mdir *mdir)
{
	mdir_dtor(mdir);
	mdir_free(mdir);
}

/*
 * lookup
 */

struct mdir *mdir_lookup_by_id(char const *id, bool create)
{
	debug("lookup id '%s', %screate", id, create ? "":"no ");
	// First try to find an existing struct mdir
	struct mdir *mdir;
	LIST_FOREACH(mdir, &mdirs, entry) {
		if (0 == strcmp(id, mdir_id(mdir))) return mdir;
	}
	// Load it
	return mdir_new(id, create);
}

struct mdir *mdir_lookup(char const *name)
{
	debug("lookup %s", name);
	// OK, so we have a name. We let the kernel do the lookup for us (symlinks),
	// then fetch the id from the result.
	if (0 == strcmp("/", name)) { // this one is not a symlink
		return mdir_lookup_by_id("root", false);
	}
	char path[PATH_MAX];
	char slink[PATH_MAX];
	snprintf(path, sizeof(path), "%s/root/%s", mdir_root, name);
	ssize_t len = readlink(path, slink, sizeof(slink));
	if (len == -1) with_error(errno, "readlink %s", path) return NULL;
	if (len == 0 || len == sizeof(slink)) with_error(0, "bad symlink for %s", path) return NULL;
	char *id = slink + len - 1;
	while (id >= slink && *id != '/') id--;
	if (id >= slink) *id = '\0';
	return mdir_lookup_by_id(id+1, false);
}

/*
 * Init
 */

void mdir_begin(bool server)
{
	if (server) mdir_namespace = 'S';
	else mdir_namespace = 'C';
	mdir_alloc = mdir_alloc_default;
	mdir_free = mdir_free_default;
	// Default configuration values
	conf_set_default_str("MDIR_ROOT_DIR", "/tmp/mdir");
	conf_set_default_str("MDIR_DIRSEQ", "/tmp/.dirid.seq");
	on_error return;
	jnl_begin();
	on_error return;
	// Inits
	LIST_INIT(&mdirs);
	mdir_root = conf_get_str("MDIR_ROOT_DIR");
	mdir_root_len = strlen(mdir_root);
	persist_ctor(&dirid_seq, sizeof(uint64_t), conf_get_str("MDIR_DIRSEQ"));
}

void mdir_end(void)
{
	jnl_end();
	struct mdir *mdir;
	while (NULL != (mdir = LIST_FIRST(&mdirs))) {
		mdir_del(mdir);
	}
	persist_dtor(&dirid_seq);
}

/*
 * Read
 */

static mdir_version get_next_version(struct mdir *mdir, struct jnl **jnl, mdir_version version)
{
	mdir_version next = 0;
	// Look for the version number that comes after the given version, and jnl
	STAILQ_FOREACH(*jnl, &mdir->jnls, entry) {
		// look first for version+1
		if ((*jnl)->version <= version+1 && (*jnl)->version + (*jnl)->nb_patches > version+1) {
			next = version+1;
			break;
		}
		if ((*jnl)->version > version+1) {	// if we can't find it, skip the gap
			next = (*jnl)->version;
			break;
		}
	}
	if (! *jnl) error_push(ENOMSG, "No more patches");
	debug("in %s, version after %"PRIversion" is %"PRIversion, mdir_id(mdir), version, next);
	return next;
}

// version is an in/out parameter
struct header *mdir_read_next(struct mdir *mdir, mdir_version *version, enum mdir_action *action)
{
	struct jnl *jnl;
	*version = get_next_version(mdir, &jnl, *version);
	on_error {
		if (error_code() == ENOMSG) error_clear();
		return NULL;
	}
	return jnl_read(jnl, *version - jnl->version, action);
}

/*
 * (Un)Link
 */

static void mdir_link(struct mdir *parent, struct header *h)
{
	debug("add link to mdir %s", mdir_id(parent));
	assert(h);
	// check that the given header does _not_ already have a dirId,
	// and add fields name and type if necessary
	char const *name = header_search(h, SCAMBIO_NAME_FIELD);
	if (! name) {
		name = "Unnamed";
		header_add_field(h, SCAMBIO_NAME_FIELD, name);
		on_error return;
	}
	struct mdir *child;
	char const *dirid = header_search(h, SCAMBIO_DIRID_FIELD);
	if (dirid) {
		// create it if it does not exist
		child = mdir_lookup_by_id(dirid, true);
		on_error return;
	} else {
		child = mdir_create();
		on_error return;
		header_add_field(h, SCAMBIO_DIRID_FIELD, mdir_id(child));
		on_error return;
	}
	char const *dirtype = header_search(h, SCAMBIO_TYPE_FIELD);
	if (dirtype) {
		if (0 != strcmp(dirtype, SCAMBIO_DIR_TYPE)) with_error(0, "Bad header type (%s)", dirtype) return;
	} else {
		header_add_field(h, SCAMBIO_TYPE_FIELD, SCAMBIO_DIR_TYPE);
		on_error return;
	}
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", parent->path, name);
	if (0 != symlink(child->path, path)) error_push(errno, "symlink %s -> %s", path, child->path);
}

static void mdir_unlink(struct mdir *parent, struct header *h)
{
	debug("remove a link from mdir %s", mdir_id(parent));
	assert(h);
	char const *name = header_search(h, SCAMBIO_NAME_FIELD);
	if (! name) with_error(0, "folder name ommited") return;
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", parent->path, name);
	if (0 != unlink(path)) error_push(errno, "unlink %s", path);
}

/*
 * Patch
 */

mdir_version mdir_patch(struct mdir *mdir, enum mdir_action action, struct header *header)
{
	debug("patch mdir %s", mdir_id(mdir));
	mdir_version version = 0;
	on_error return 0;
	// First acquire writer-grade lock
	(void)pth_rwlock_acquire(&mdir->rwlock, PTH_RWLOCK_RW, FALSE, NULL);	// better use a reader/writer lock (we also need to lock out readers!)
	do {
		// if its for a subfolder, performs the (un)linking
		if (header_is_directory(header))  {
			if (action == MDIR_ADD) {
				mdir_link(mdir, header);
			} else {
				mdir_unlink(mdir, header);
			}
			on_error break;
		}
		// Either use the last journal, or create a new one
		struct jnl *jnl = STAILQ_LAST(&mdir->jnls, jnl, entry);
		if (! jnl) {
			jnl = jnl_new_empty(mdir, 1);
		} else if (jnl_too_big(jnl)) {
			jnl = jnl_new_empty(mdir, jnl->version + jnl->nb_patches);
		}
		on_error break;
		version = jnl_patch(jnl, action, header);
	} while (0);
	(void)pth_rwlock_release(&mdir->rwlock);
	return version;
}

void mdir_patch_request(struct mdir *mdir, enum mdir_action action, struct header *h)
{
	bool is_dir = header_is_directory(h);
	char const *dirId = is_dir ? header_search(h, SCAMBIO_DIRID_FIELD):NULL;
	if (dirId && dirId[0] == mdir_namespace) with_error(0, "Cannot refer to a temporary dirId") return;
	char temp[PATH_MAX];
	int len = snprintf(temp, sizeof(temp), "%s/.temp/%c", mdir->path, action == MDIR_ADD ? '+':'-');
	Mkdir(temp);
	on_error return;	// this is not an error if the dir already exists
	header_digest(h, sizeof(temp)-len, temp+len);
	header_to_file(h, temp);
	on_error return;
	if (is_dir) {
		if (action == MDIR_ADD) {
			mdir_link(mdir, h);
		} else {
			mdir_unlink(mdir, h);
		}
	}
}

/*
 * List
 */

// sync means in on the server - but not necessarily on our log yet
void mdir_patch_list(struct mdir *mdir, bool want_sync, bool want_unsync, void (*cb)(struct mdir *, struct header *, enum mdir_action action, bool confirmed, mdir_version version))
{
	struct jnl *jnl;
	if (want_sync) {
		// List content of journals
		debug("listing journal");
		STAILQ_FOREACH(jnl, &mdir->jnls, entry) {
			for (unsigned index=0; index < jnl->nb_patches; index++) {
				enum mdir_action action;
				struct header *h = jnl_read(jnl, index, &action);
				on_error return;
				cb(mdir, h, action, true, jnl->version + index);
				header_del(h);
			}
		}
	}
	if (want_unsync) {
		// List content of ".tmp" subdirectory
		char temp[PATH_MAX];
		int dirlen = snprintf(temp, sizeof(temp), "%s/.tmp", mdir->path);
		debug("listing %s", temp);
		DIR *dir = opendir(temp);
		if (! dir) {
			if (errno != ENOENT) goto end_unsync;
			with_error(errno, "opendir(%s)", temp) return;
		}
		mdir_version last_version = mdir_last_version(mdir);
		struct dirent *dirent;
		while (NULL != (dirent = readdir(dir))) {
			char const *const filename = dirent->d_name;
			// patch files start with the action code '+' or '-'.
			enum mdir_action action;
			if (filename[0] == '+') action = MDIR_ADD;
			else if (filename[0] == '-') action = MDIR_REM;
			else continue;
			snprintf(temp+dirlen, sizeof(temp)-dirlen, "/%s", filename);
			// then filename may be either a numeric key (patch was not sent yet)
			// or "=version" (patch was sent and acked with this version number)
			bool synced = filename[1] == '=';
			mdir_version version = 0;
			if (synced) {	// maybe we already synchronized it down into the journal ?
				version = mdir_str2version(filename+2);
				on_error {
					error("please fix %s", temp);
					error_clear();
					continue;
				}
				if (version <= last_version) {	// already on our journal so remove it
					debug("unlinking %s", temp);
					if (0 != unlink(temp)) with_error(errno, "unlink(%s)", temp) return;
					continue;
				}
				if (! want_sync) continue;
			}
			struct header *h = header_from_file(temp);
			on_error return;
			cb(mdir, h, action, synced, version);
			header_del(h);
		}
		if (closedir(dir) < 0) with_error(errno, "closedir(%s)", temp) return;
	}
end_unsync:;
}

/*
 * Accessors
 */

mdir_version mdir_last_version(struct mdir *mdir)
{
	struct jnl *jnl = STAILQ_LAST(&mdir->jnls, jnl, entry);
	if (! jnl) return 0;
	return jnl->version + jnl->nb_patches - 1;
}

char const *mdir_id(struct mdir *mdir)
{
	return mdir->path + mdir_root_len + 1;
}

/*
 * Utilities
 */

char const *mdir_version2str(mdir_version version)
{
	static char str[20+1];
	int len = snprintf(str, sizeof(str), "%"PRIversion, version);
	assert(len < (int)sizeof(str));
	return str;
}

mdir_version mdir_str2version(char const *str)
{
	mdir_version version;
	if (1 != sscanf(str, "%"PRIversion, &version)) with_error(0, "sscanf(%s)", str) return 0;
	return version;
}

char const *mdir_action2str(enum mdir_action action)
{
	switch (action) {
		case MDIR_ADD: return "+";
		case MDIR_REM: return "-";
	}
	assert(0);
	return "INVALID";
}

enum mdir_action mdir_str2action(char const *str)
{
	if (str[1] != '\0') with_error(0, "Invalid action : '%s'", str) return MDIR_ADD;
	switch (str[0]) {
		case '+': return MDIR_ADD;
		case '-': return MDIR_REM;
	}
	with_error(0, "Invalid action : '%s'", str) return MDIR_ADD;
}

