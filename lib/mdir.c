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
#include "misc.h"
#include "digest.h"
#include "persist.h"
#include "jnl.h"
#include "channel.h"

/*
 * Data Definitions
 */

static size_t mdir_root_len;
static char const *mdir_root;
static LIST_HEAD(mdirs, mdir) mdirs;
static struct persist dirid_seq;
static struct persist transient_version;
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

static void mdir_empty(struct mdir *mdir)
{
	struct jnl *jnl;
	while (NULL != (jnl = STAILQ_FIRST(&mdir->jnls))) {
		jnl_del(jnl);
	}
}

static void mdir_reload(struct mdir *mdir)
{
	mdir_empty(mdir);
	// Find journals
	DIR *d = opendir(mdir->path);
	if (! d) with_error(errno, "opendir %s", mdir->path) return;
	struct dirent *dirent;
	while (NULL != (dirent = readdir(d))) {
		if (is_jnl_file(dirent->d_name)) {
			jnl_new(mdir, dirent->d_name);
			on_error break;
		}
	}
	if (0 != closedir(d)) error_push(errno, "Cannot closedir %s", mdir->path);
}

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
	mdir_reload(mdir);
	unless_error LIST_INSERT_HEAD(&mdirs, mdir, entry);
	mdir->last_listed_sync = 0;
	mdir->last_listed_unsync = 0;
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

static struct mdir *mdir_create(bool transient)
{
	debug("create a new mdir");
	uint64_t id;
	if_fail (id = persist_read_inc_sequence(&dirid_seq)) return NULL;
	char id_str[1+20+1];
	snprintf(id_str, sizeof(id_str), "%s%"PRIu64, transient ? "_":"", id);
	struct mdir *mdir = mdir_new(id_str, true);
	assert(transient == mdir_is_transient(mdir));
	return mdir;
}

static void mdir_dtor(struct mdir *mdir)
{
	(void)pth_rwlock_acquire(&mdir->rwlock, PTH_RWLOCK_RW, FALSE, NULL);
	LIST_REMOVE(mdir, entry);
	mdir_empty(mdir);
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
	debug("not found on cache");
	return mdir_new(id, create);
}

static struct mdir *lookup_abs(char const *path)
{
	debug("lookup absolute path '%s'", path);
	char slink[PATH_MAX];
	ssize_t len = readlink(path, slink, sizeof(slink));
	if (len == -1) with_error(errno, "readlink %s", path) return NULL;
	if (len == 0 || len >= (int)sizeof(slink) || len <= (int)mdir_root_len+1) with_error(0, "bad symlink for %s", path) return NULL;
	slink[len] = '\0';
	return mdir_lookup_by_id(slink+mdir_root_len+1, false);	// symlinks points to "mdir_root/id"
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
	snprintf(path, sizeof(path), "%s/root/%s", mdir_root, name);
	return lookup_abs(path);
}

/*
 * Init
 */

void mdir_begin(void)
{
	mdir_alloc = mdir_alloc_default;
	mdir_free = mdir_free_default;
	// Default configuration values
	conf_set_default_str("MDIR_ROOT_DIR", "/var/lib/scambio/mdir");
	conf_set_default_str("MDIR_DIRSEQ", "/var/lib/scambio/mdir/.dirid.seq");
	conf_set_default_str("MDIR_TRANSIENTSEQ", "/var/lib/scambio/mdir/.transient.seq");
	on_error return;
	if_fail (jnl_begin()) return;
	// Inits
	LIST_INIT(&mdirs);
	mdir_root = conf_get_str("MDIR_ROOT_DIR");
	mdir_root_len = strlen(mdir_root);
	char root_path[PATH_MAX];
	snprintf(root_path, sizeof(root_path), "%s/root", mdir_root);
	if_fail (Mkdir(root_path)) return;
	persist_ctor_sequence(&dirid_seq, conf_get_str("MDIR_DIRSEQ"), 0);
	persist_ctor_sequence(&transient_version, conf_get_str("MDIR_TRANSIENTSEQ"), 1);
}

void mdir_end(void)
{
	jnl_end();
	struct mdir *mdir;
	while (NULL != (mdir = LIST_FIRST(&mdirs))) {
		mdir_del(mdir);
	}
	persist_dtor(&dirid_seq);
	persist_dtor(&transient_version);
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
	struct header *header = NULL;
	do {
		*version = get_next_version(mdir, &jnl, *version);
		on_error {
			if (error_code() == ENOMSG) error_clear();
			return NULL;
		}
		header = jnl_read(jnl, *version - jnl->version, action);
	} while (! header);
	return header;
}

static struct header *get_header(struct mdir *mdir, mdir_version version, enum mdir_action *action)
{
	struct jnl *jnl;
	struct header *header;
	// Look for the jnl
	STAILQ_FOREACH(jnl, &mdir->jnls, entry) {
		if (jnl->version <= version && jnl->version + jnl->nb_patches > version) {
			break;
		}
	}
	if (! jnl) with_error(0, "No such version (%"PRIversion")", version) return NULL;
	// Extract header and action
	if_fail (header = jnl_read(jnl, version-jnl->version, action)) return NULL;
	return header;
}

/*
 * (Un)Link
 */

// FIXME : on error, will left the directory in unusable state
static void mdir_link(struct mdir *parent, struct header *h, bool transient)
{
	debug("add link to mdir %s", mdir_id(parent));
	assert(h);
	// check that the given header does _not_ already have a dirId,
	// and add fields name and type if necessary
	char const *name = header_search(h, SC_NAME_FIELD);
	if (! name) {
		name = "Unnamed";
		header_add_field(h, SC_NAME_FIELD, name);
		on_error return;
	}
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", parent->path, name);
	struct mdir *child;
	char const *dirid = header_search(h, SC_DIRID_FIELD);
	if (dirid) {
		assert(! transient);
		// A symlink to a transient dirId may already exist. If so, rename it.
		// Otherwise, create a new one.
		char prev_link[PATH_MAX];
		ssize_t len = readlink(path, prev_link, sizeof(prev_link));
		if (len == -1) {
			if (errno != ENOENT) with_error(errno, "Cannot readlink %s", path) return;
			// does not exist : then create a new one
			child = mdir_lookup_by_id(dirid, true);
		} else {	// the symlink exist
			assert(len < (int)sizeof(prev_link) && len > 0);	// PATH_MAX is supposed to be able to store a path + the nul byte
			prev_link[len] = '\0';
			// check it points toward a temporary dirId
			char *prev_dirid = prev_link + len;
			while (prev_dirid > prev_link && *(prev_dirid-1) != '/') prev_dirid--;
			debug("found a previous link to '%s'", prev_dirid);
			if (*prev_dirid != '_') with_error(0, "Previous link for new dirId %s points toward non transient dirId %s", dirid, prev_dirid) return;
			// rename dirId and rebuild symlink
			char new_path[PATH_MAX];
			snprintf(new_path, sizeof(new_path), "%s/%s", mdir_root, prev_dirid);
			if (0 != rename(prev_link, new_path)) with_error(errno, "Cannot rename transient dirId %s to %s", prev_link, new_path) return;
			if (0 != unlink(path)) with_error(errno, "Cannot remove previous symlink %s", path) return;
			child = mdir_lookup_by_id(dirid, false);	// must exists by now
			on_error return;
			// new symlink is created below
		}
		on_error return;
	} else {
		debug("no dirId in header (yet)");
		child = mdir_create(transient);
		on_error return;
		if (! transient) {
			header_add_field(h, SC_DIRID_FIELD, mdir_id(child));
			on_error return;
		}
	}
	debug("symlinking %s to %s", path, child->path);
	if (0 != symlink(child->path, path)) error_push(errno, "symlink %s -> %s", path, child->path);
}

static void mdir_unlink(struct mdir *parent, struct header *h)
{
	debug("remove a link from mdir %s", mdir_id(parent));
	assert(h);
	char const *name = header_search(h, SC_NAME_FIELD);
	if (! name) with_error(0, "folder name ommited") return;
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", parent->path, name);
	if (0 != unlink(path)) error_push(errno, "unlink %s", path);
}

/*
 * Patch
 */

static struct jnl *last_jnl(struct mdir *mdir)
{
	// Either use the last journal, or create a new one
	struct jnl *jnl = STAILQ_LAST(&mdir->jnls, jnl, entry);
	if (! jnl) {
		jnl = jnl_new_empty(mdir, 1);
	} else if (jnl_too_big(jnl)) {
		jnl = jnl_new_empty(mdir, jnl->version + jnl->nb_patches);
	}
	return jnl;
}

static struct jnl *find_jnl(struct mdir *mdir, mdir_version version)
{
	struct jnl *jnl;
	STAILQ_FOREACH(jnl, &mdir->jnls, entry) {
		if (jnl->version <= version && jnl->version + jnl->nb_patches > version) return jnl;
	}
	return NULL;
}

static void insert_blank_patches(struct mdir *mdir, unsigned nb)
{
	debug("insert %u blank patch(es)", nb);
	struct jnl *jnl;
	while (nb --) {
		if_fail (jnl = last_jnl(mdir)) break;
		if_fail ((void)jnl_patch_blank(jnl)) break;
	}
}

mdir_version mdir_patch(struct mdir *mdir, enum mdir_action action, struct header *header, unsigned nb_deleted)
{
	debug("patch mdir %s (with %u dels)", mdir_id(mdir), nb_deleted);
	mdir_version version = 0;
	// First acquire writer-grade lock
	(void)pth_rwlock_acquire(&mdir->rwlock, PTH_RWLOCK_RW, FALSE, NULL);	// better use a reader/writer lock (we also need to lock out readers!)
	do {
		// We may want to insert some blank entry before this one
		if (nb_deleted) if_fail (insert_blank_patches(mdir, nb_deleted)) break;
		// If it's a removal, check the header and the deleted version
		if (action == MDIR_REM) {
			mdir_version to_del = header_target(header);
			on_error break;
			struct jnl *old_jnl = find_jnl(mdir, to_del);
			on_error break;
			if (! old_jnl) with_error(0, "Version %"PRIversion" does not exist", to_del) break;
			if_fail (jnl_mark_del(old_jnl, to_del)) break;
		}
		// if its for a subfolder, performs the (un)linking
		if (header_is_directory(header)) {
			if (action == MDIR_ADD) {
				mdir_link(mdir, header, false);
			} else {
				mdir_unlink(mdir, header);
			}
			on_error break;
		}
		// Either use the last journal, or create a new one
		struct jnl *jnl = last_jnl(mdir);
		on_error break;
		version = jnl_patch(jnl, action, header);
	} while (0);
	(void)pth_rwlock_release(&mdir->rwlock);
	return version;
}

void mdir_patch_request(struct mdir *mdir, enum mdir_action action, struct header *h)
{
	bool is_dir = header_is_directory(h);
	char const *dirId = is_dir ? header_search(h, SC_DIRID_FIELD):NULL;
	if (dirId && dirId[0] == '_') with_error(0, "Cannot refer to a temporary dirId") return;
	char temp[PATH_MAX];
	int len = snprintf(temp, sizeof(temp), "%s/.tmp/", mdir->path);
	Mkdir(temp);
	on_error {	// this is not an error if the dir already exists
		if (error_code() == EEXIST) error_clear();
		else return;
	}
	mdir_version tver;
	if_fail (tver = persist_read_inc_sequence(&transient_version)) return;
	snprintf(temp+len, sizeof(temp)-len, "%c%"PRIversion, action == MDIR_ADD ? '+':'-', tver);
	int fd = open(temp, O_WRONLY|O_CREAT|O_EXCL, 0440);
	if (fd < 0) with_error(errno, "Cannot create transient patch in %s", temp) return;
	header_write(h, fd);
	(void)close(fd);
	on_error return;
	if (is_dir) {
		if (action == MDIR_ADD) {
			mdir_link(mdir, h, true);
		} else {
			mdir_unlink(mdir, h);
		}
	}
}

void mdir_del_request(struct mdir *mdir, mdir_version to_del)
{
	assert(to_del != 0);
	struct header *h = header_new();
	on_error return;
	header_add_field(h, SC_TARGET_FIELD, mdir_version2str(to_del));
	unless_error mdir_patch_request(mdir, MDIR_REM, h);
	header_del(h);
}

/*
 * List
 */

static mdir_version get_target(struct header *h)
{
	char const *target_str = header_search(h, SC_TARGET_FIELD);
	if (! target_str) with_error(0, "Removal patch with no target ?") return 0;
	return mdir_str2version(target_str);
}

void mdir_patch_list(struct mdir *mdir, bool unsync_only, void (*cb)(struct mdir *, struct header *, enum mdir_action action, mdir_version, void *), void *data)
{
	mdir_reload(mdir);	// journals may have changed, other appended
	struct jnl *jnl;
	if (! unsync_only) { // List content of journals
		mdir_version from = mdir->last_listed_sync + 1;
		debug("listing synched from version %"PRIversion, from);
		STAILQ_FOREACH(jnl, &mdir->jnls, entry) {
			if (jnl->version + jnl->nb_patches <= from) {
				debug("  jnl starting at %"PRIversion" too short (%u)", jnl->version, jnl->nb_patches);
				continue;
			}
			if (from < jnl->version) from = jnl->version;	// a gap
			for (unsigned index = from - jnl->version; index < jnl->nb_patches; index++) {
				enum mdir_action action;
				struct header *h = jnl_read(jnl, index, &action);
				on_error return;
				if (! h) continue;
				bool skip = false;
				char const *localid = header_search(h, SC_LOCALID_FIELD);
				if (localid) {	// then the local file does not exist anymore, but we may already have reported it when it was present
					mdir_version local = mdir_str2version(localid);
					on_error {
						error_clear();
					} else {
						if (local <= mdir->last_listed_unsync) skip = true;	// we already reported it
					}
				}
				if (action == MDIR_REM) {	// don't report removals of patches we did not report
					mdir_version target = get_target(h);
					on_error {
						error_clear();
						skip = true;
					} else if (target >= from) {
						skip = true;
					}
				}
				if (! skip) {
					cb(mdir, h, action, jnl->version + index, data);
				}
				header_del(h);
				on_error return;
				mdir->last_listed_sync = jnl->version + index;
			}
		}
	}
	// List content of ".tmp" subdirectory which holds both synched and not yet synched patches.
	char temp[PATH_MAX];
	int dirlen = snprintf(temp, sizeof(temp), "%s/.tmp", mdir->path);
	debug("listing %s from transient version %"PRIversion, temp, mdir->last_listed_unsync);
	DIR *dir = opendir(temp);
	if (! dir) {
		if (errno == ENOENT) return;
		with_error(errno, "opendir(%s)", temp) return;
	}
	mdir_version last_version = mdir_last_version(mdir);	// used to remove transient acked patches already on the journals (ie synched down)
	struct dirent *dirent;
	while (NULL != (dirent = readdir(dir))) {
		char const *const filename = dirent->d_name;
		debug("  considering '%s'", filename);
		// patch files start with the action code '+' or '-'.
		enum mdir_action action;
		if (filename[0] == '+') action = MDIR_ADD;
		else if (filename[0] == '-') action = MDIR_REM;
		else continue;
		snprintf(temp+dirlen, sizeof(temp)-dirlen, "/%s", filename);
		// then filename may be either a transient version number (patch was not sent or not acked yet)
		// or "=version" (patch was sent and acked with this version number)
		bool acked = filename[1] == '=';
		mdir_version version = 0;
		if (acked) {	// maybe we already synchronized it down into the journal ?
			if_fail (version = mdir_str2version(filename+2)) {
				error("please fix %s", temp);
				error_clear();
				continue;
			}
			if (version <= last_version) {	// already on our journal so remove it
				debug("unlinking %s", temp);
				if (0 != unlink(temp)) with_error(errno, "unlink(%s)", temp) break;
				continue;
			}
			if (unsync_only) continue;
			if (version <= mdir->last_listed_sync) continue;
			mdir->last_listed_sync = version;
		} else {	// not acked
			if_fail (version = mdir_str2version(filename+1)) {
				error("please fix %s", temp);
				error_clear();
				continue;
			}
			if (version <= mdir->last_listed_unsync) continue;
			mdir->last_listed_unsync = version;
		}
		struct header *h = header_from_file(temp);
		on_error break;
		cb(mdir, h, action, acked ? version:-version, data);
		header_del(h);
		on_error break;
	}
	if (closedir(dir) < 0) with_error(errno, "closedir(%s)", temp) return;
}

void mdir_patch_reset(struct mdir *mdir)
{
	mdir->last_listed_sync = mdir->last_listed_unsync = 0;
}

void mdir_folder_list(struct mdir *mdir, bool new_only, void (*cb)(struct mdir *, struct mdir *, bool, char const *, void *), void *data)
{
	DIR *d = opendir(mdir->path);
	if (! d) with_error(errno, "opendir %s", mdir->path) return;
	struct dirent *dirent;
	while (NULL != (dirent = readdir(d))) {
		struct stat statbuf;
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", mdir->path, dirent->d_name);
		if (0 != lstat(path, &statbuf)) with_error(errno, "lstat %s", path) break;
		if (! S_ISLNK(statbuf.st_mode)) continue;
		struct mdir *child = lookup_abs(path);
		on_error break;
		bool new = mdir_is_transient(child);
		if (!new && new_only) continue;
		char *name = strdup(dirent->d_name);
		if (! name) with_error(errno, "strdup") break;
		cb(mdir, child, new, name, data);
		free(name);
		on_error break;
	}
	if (0 != closedir(d)) error_push(errno, "Cannot closedir %s", mdir->path);
	return;
}

struct header *mdir_get_targeted_header(struct mdir *mdir, struct header *h)
{
	mdir_version version;
	struct header *header;
	enum mdir_action action;
	if_fail (version = get_target(h)) return NULL;
	if_fail (header = get_header(mdir, version, &action)) return NULL;
	if (action != MDIR_ADD) with_error(0, "Target header was not an addition !") return NULL;
	return header;
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

bool mdir_is_transient(struct mdir *mdir)
{
	return mdir_id(mdir)[0] == '_';
}

// count only the patch that are not removed
static void count_patch(struct mdir *mdir, struct header *header, enum mdir_action action, mdir_version version, void *data)
{
	(void)mdir;
	(void)version;
	unsigned *size = (unsigned *)data;
	if (header_is_directory(header)) return; // exclude directories
	assert(action == MDIR_ADD || version < 0);
	(*size) += action == MDIR_ADD ? 1:-1;
}

unsigned mdir_size(struct mdir *mdir, bool unsync_only)
{
	unsigned size = 0;
	mdir_version last_listed_sync_bkp = mdir->last_listed_sync;
	mdir_version last_listed_unsync_bkp = mdir->last_listed_unsync;
	mdir_patch_reset(mdir);
	mdir_patch_list(mdir, unsync_only, count_patch, &size);
	mdir->last_listed_sync = last_listed_sync_bkp;
	mdir->last_listed_unsync = last_listed_unsync_bkp;
	return size;
}
