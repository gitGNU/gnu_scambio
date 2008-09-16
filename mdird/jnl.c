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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pth.h>
#include "scambio.h"
#include "varbuf.h"
#include "log.h"
#include "jnl.h"
#include "header.h"
#include "misc.h"
#include "sub.h"
#include "stribution.h"
#include "digest.h"

/*
 * Data Definitions
 *
 * We have some cache here. Beware that they keep coherency at pth scheduling points.
 */

static char const *rootdir;
static size_t rootdir_len;
static unsigned max_jnl_size;
static char const *strib_fname;

struct index_entry {
	off_t offset;
};

struct jnl {
	STAILQ_ENTRY(jnl) entry;
	int idx_fd;	// to the index file
	int patch_fd;	// to the patch file
	long long version;	// version of the first patch in this journal
	unsigned nb_patches;	// number of patches in this file
	struct dir *dir;
};

struct dir {
	LIST_ENTRY(dir) entry;	// entry in the list of cached dirs (FIXME: hash this!)
	STAILQ_HEAD(jnls, jnl) jnls;	// list all jnl in this directory (refreshed from time to time), ordered by first_version
	LIST_HEAD(dir_subs, subscription) subscriptions;
	dev_t st_dev;	// st_dev and st_ino are used to identify directories
	ino_t st_ino;
	pth_rwlock_t rwlock;
	size_t path_len;
	char path[PATH_MAX];
	struct stribution *strib;	// classify configuration
};

static LIST_HEAD(dirs, dir) dirs;

static char const *digest_field = "scambio-digest";

/*
 * Jnls
 */

#define JNL_FNAME_FORMAT "%020lld.idx"
#define JNL_FNAME_LEN 24

#define PRIjnl "s/%020lld"
#define PRIJNL(j) (j)->dir->path, (j)->version

static int parse_version(char const *filename, long long *version)
{
	char *end;
	*version = strtoll(filename, &end, 10);
	if (0 != strcmp(end, ".idx")) {
		debug("'%s' is not a journal file", filename);
		return -1;
	}
	return 0;
}

static void restore_dir_path(struct dir *dir)
{
	dir->path[dir->path_len] = '\0';
}

static int append_to_dir_path(struct dir *dir, char const *filename)
{
	size_t len = snprintf(dir->path+dir->path_len, sizeof(dir->path)-dir->path_len, "/%s", filename);
	if (len >= sizeof(dir->path)-dir->path_len) {
		restore_dir_path(dir);
		return -ENAMETOOLONG;
	}
	return 0;
}

static int filesize(off_t *size, int fd)
{
	*size = lseek(fd, 0, SEEK_END);
	if ((off_t)-1 == *size) return -errno;
	return 0;
}

static int fetch_nb_patches(unsigned *nbp, int fd)
{
	off_t size;
	int err = filesize(&size, fd);
	if (! err) {
		assert(0 == size % sizeof(struct index_entry));
		*nbp = size / sizeof(struct index_entry);
	}
	return err;
}

// Caller must have a writer lock on dir
// Create the files if they do not exist
static int jnl_ctor(struct jnl *jnl, struct dir *dir, char const *filename)
{
	int err = 0;
	// Check filename
	if (0 != parse_version(filename, &jnl->version)) return -EINVAL;
	// Open all files
	if (0 != (err = append_to_dir_path(dir, filename))) return err;
	jnl->idx_fd = open(dir->path, O_RDWR|O_APPEND|O_CREAT, 0660);
	memcpy(dir->path + dir->path_len + 1 + JNL_FNAME_LEN - 3, "log", 4);	// change extention
	if (jnl->idx_fd >= 0) jnl->patch_fd = open(dir->path, O_RDWR|O_APPEND|O_CREAT, 0660);	// do not overwrite errno
	restore_dir_path(dir);
	if (jnl->idx_fd < 0 || jnl->patch_fd < 0) return -errno;
	// get sizes
	if (0 != (err = fetch_nb_patches(&jnl->nb_patches, jnl->idx_fd))) return err;
	// All is OK, insert it as a journal
	if (STAILQ_EMPTY(&dir->jnls)) {
		STAILQ_INSERT_HEAD(&dir->jnls, jnl, entry);
	} else {	// insert in version order
		struct jnl *j, *prev = NULL;
		STAILQ_FOREACH(j, &dir->jnls, entry) {
			if (j->version > jnl->version) break;
			assert(j->version < jnl->version);
			assert(j->version + j->nb_patches <= jnl->version);
			prev = j;
		}
		if (prev) {
			STAILQ_INSERT_AFTER(&dir->jnls, prev, jnl, entry);
		} else {
			STAILQ_INSERT_HEAD(&dir->jnls, jnl, entry);
		}
	}
	jnl->dir = dir;
	return 0;
}

static void jnl_dtor(struct jnl *jnl)
{
	STAILQ_REMOVE(&jnl->dir->jnls, jnl, jnl, entry);
	if (jnl->idx_fd >= 0) (void)close(jnl->idx_fd);
	jnl->idx_fd = -1;
	if (jnl->patch_fd >= 0) (void)close(jnl->patch_fd);
	jnl->patch_fd = -1;
}

static int jnl_new(struct jnl **jnl, struct dir *dir, char const *filename)
{
	*jnl = malloc(sizeof(**jnl));
	if (! *jnl) return -ENOMEM;
	int err = jnl_ctor(*jnl, dir, filename);
	if (err) {
		free(*jnl);
		*jnl = NULL;
	}
	return err;
}

static void jnl_del(struct jnl *jnl)
{
	jnl_dtor(jnl);
	free(jnl);
}

/*
 * Cached dir
 */

static int dir_ctor(struct dir *dir, size_t path_len, char const *path)
{
	int err = 0;
	struct stat statbuf;
	if (0 != (err = stat(path, &statbuf))) return err;
	dir->path_len = path_len;
	memcpy(dir->path, path, path_len+1);
	dir->st_dev = statbuf.st_dev;
	dir->st_ino = statbuf.st_ino;
	(void)pth_rwlock_init(&dir->rwlock);	// FIXME: if this can schedule some other thread, we need to prevent addition of the same dir twice
	STAILQ_INIT(&dir->jnls);
	// load classification configuration
	if (0 != (err = append_to_dir_path(dir, strib_fname))) return err;
	dir->strib = strib_new(dir->path);
	restore_dir_path(dir);
	// Find journals
	DIR *d = opendir(dir->path);
	if (! d) return -errno;
	struct dirent *dirent;
	errno = 0;
	// We may yield the CPU here, but then we must acquire write lock
	while (NULL != (dirent = readdir(d))) {
		struct jnl *dummy;
		int e;
		if (0 != (e = jnl_new(&dummy, dir, dirent->d_name))) {
			warning("Cannot add journal '%s/%s' : %s", dir->path, dirent->d_name, strerror(-e));
		}
	}
	if (errno) err = -errno;
	if (0 != closedir(d)) error("Cannot closedir('%s') : %s", dir->path, strerror(errno));	// ignore this
	if (! err) LIST_INSERT_HEAD(&dirs, dir, entry);
	return err;
}

static void dir_dtor(struct dir *dir)
{
	assert(LIST_EMPTY(&dir->subscriptions));
	(void)pth_rwlock_acquire(&dir->rwlock, PTH_RWLOCK_RW, FALSE, NULL);
	LIST_REMOVE(dir, entry);
	struct jnl *jnl;
	while (NULL != (jnl = STAILQ_FIRST(&dir->jnls))) {
		jnl_del(jnl);
	}
	if (dir->strib) {
		strib_del(dir->strib);
		dir->strib = NULL;
	}
	pth_rwlock_release(&dir->rwlock);
}

static int dir_new(struct dir **dir, size_t path_len, char const *path)
{
	int err = 0;
	*dir = malloc(sizeof(**dir));
	if (! *dir) return -ENOMEM;
	if (0 != (err = dir_ctor(*dir, path_len, path))) {
		free(*dir);
		*dir = NULL;
	}
	return err;
}

static void dir_del(struct dir *dir)
{
	dir_dtor(dir);
	free(dir);
}

bool dir_same_path(struct dir *dir, char const *path)
{
	char abspath[PATH_MAX];	// FIXME: change directory once and for all !
	snprintf(abspath, sizeof(abspath), "%s/root/%s", rootdir, path);
	struct stat statbuf;
	if (0 != stat(abspath, &statbuf)) {
		warning("Cannot stat '%s' : %s", abspath, strerror(errno));
		return false;	// for the purpose of this predicate we don't need system error
	}
	return dir->st_dev == statbuf.st_dev && dir->st_ino == statbuf.st_ino;
}

/*
 * (De)Initialization
 */

int jnl_begin(void)
{
	int err;
	// Default configuration values
	if (0 != (err = conf_set_default_str("MDIRD_ROOT_DIR", "/tmp/mdir"))) return err;
	if (0 != (err = conf_set_default_int("MDIRD_MAX_JNL_SIZE", 2000))) return err;
	if (0 != (err = conf_set_default_str("MDIRD_STRIB_FNAME", ".stribution.conf"))) return err;
	// Inits
	LIST_INIT(&dirs);
	rootdir = conf_get_str("MDIRD_ROOT_DIR");
	rootdir_len = strlen(rootdir);
	max_jnl_size = conf_get_int("MDIRD_MAX_JNL_SIZE");
	strib_fname = conf_get_str("MDIRD_STRIB_FNAME");
	return err;
}

void jnl_end(void)
{
	struct dir *dir;
	while (NULL != (dir = LIST_FIRST(&dirs))) {
		dir_del(dir);
	}
}

/*
 * Searching for a version
 */

int dir_get(struct dir **dir, char const *path)
{
	int err;
	char abspath[PATH_MAX];
	if (strstr(path, "/../")) return -EACCES;
	size_t path_len = snprintf(abspath, sizeof(abspath), "%s/root/%s", rootdir, path);
	if (path_len >= sizeof(abspath)) return -ENAMETOOLONG;
	while (path_len > 1 && abspath[path_len-1] == '/') {	// removes unnecessary slashes
		abspath[--path_len] = '\0';
	}
	// We compare inodes, which is faster than to compare paths
	struct stat statbuf;
	if (0 != (err = stat(abspath, &statbuf))) {
		return -errno;
	}
	LIST_FOREACH(*dir, &dirs, entry) {
		if ((*dir)->st_dev == statbuf.st_dev && (*dir)->st_ino == statbuf.st_ino) {
			return 0;
		}
	}
	// TODO: uncache some dir from time to time
	if (0 != (err = dir_new(dir, path_len, abspath))) return err;
	return 0;
}

int dir_exist(char const *path)
{
	char abspath[PATH_MAX];
	snprintf(abspath, sizeof(abspath), "%s/root/%s", rootdir, path);
	struct stat statbuf;
	if (0 != stat(abspath, &statbuf)) {
		if (errno == ENOENT) return 0;
		return -errno;
	}
	if (! S_ISDIR(statbuf.st_mode)) return -ENOTDIR;
	return 1;
}

int strib_get(struct stribution **stribp, char const *path)
{
	struct dir *dir;
	int err = dir_get(&dir, path);
	if (! err) *stribp = dir->strib;
	return err;
}

/*
 * Append an action into the journal.
 * Require writer's lock
 */

static int write_patch(struct jnl *jnl, char action, struct header *header)
{
	int err = 0;
	struct index_entry ie;
	if (0 != (err = filesize(&ie.offset, jnl->patch_fd))) return err;
	// Write the index
	if (0 != (err = Write(jnl->idx_fd, &ie, sizeof(ie)))) return err;
	// Then the patch command
	char action_str[2] = { action, '\n' };
	if (0 != (err = Write(jnl->patch_fd, action_str, sizeof(action_str)))) return err;
	// Then the patch (meta datas)
	if (action == '-') {
		// if this is a suppression, we can replace the content by a single field header with the digest,
		// at the condition that the incomming header itself is not a digest (or any single field header ?).
		// TODO: why not use the version number, again ?
		char digest_val[MAX_DIGEST_STRLEN+1];
		if (0 != (err = header_digest(header, sizeof(digest_val), digest_val))) return err;
		struct header *alternate_header;
		if (0 == (err = header_new(&alternate_header))) {
			if (0 == (err = header_add_field(alternate_header, digest_field, digest_val))) {
				err = header_write(alternate_header, jnl->patch_fd);
			}
			header_del(alternate_header);
		}
	} else {	// plain header
		err = header_write(header, jnl->patch_fd);
	}
	if (0 != err) return err;
	jnl->nb_patches ++;
	return 0;
}

static int add_empty_jnl(struct jnl **jnl, struct dir *dir, long long start_version)
{
	char fname[JNL_FNAME_LEN + 1];	// A little circumvoluted to create the filename first...
	snprintf(fname, sizeof(fname), JNL_FNAME_FORMAT, start_version);
	return jnl_new(jnl, dir, fname);
}

static bool jnl_too_big(struct jnl *jnl)
{
	return jnl->nb_patches >= max_jnl_size;
}

int jnl_add_patch(char const *path, char action, struct header *header)
{
	int err = 0;
	struct dir *dir = NULL;
	if (0 != (err = dir_get(&dir, path))) return err;
	// First acquire writer-grade lock
	(void)pth_rwlock_acquire(&dir->rwlock, PTH_RWLOCK_RW, FALSE, NULL);	// better use a reader/writer lock (we also need to lock out readers!)
	// Either use the last journal, or create a new one
	struct jnl *jnl = STAILQ_LAST(&dir->jnls, jnl, entry);
	if (! jnl) {
		err = add_empty_jnl(&jnl, dir, 1);
	} else if (jnl_too_big(jnl)) {
		err = add_empty_jnl(&jnl, dir, jnl->version + jnl->nb_patches);
	}
	if (! err) err = write_patch(jnl, action, header);
	(void)pth_rwlock_release(&dir->rwlock);
	return err;
}

long long dir_last_version(struct dir *dir)
{
	struct jnl *jnl = STAILQ_LAST(&dir->jnls, jnl, entry);
	if (! jnl) return 0;
	return jnl->version + jnl->nb_patches - 1;
}

void dir_register_subscription(struct dir *dir, struct subscription *sub)
{
	LIST_INSERT_HEAD(&dir->subscriptions, sub, dir_entry);
}

/*
 * Send a patch
 */

static int load_index_entry(off_t *offset, size_t *size, struct jnl *jnl, unsigned num)
{
	int err;
	struct index_entry ie[2];
	if (num < jnl->nb_patches - 1) {	// can read 2 entires
		if (0 != (err = Read(ie, jnl->idx_fd, num*sizeof(*ie), 2*sizeof(*ie)))) return err;
	} else {	// at end of index file, will need log file size
		if (0 != (err = Read(ie, jnl->idx_fd, num*sizeof(*ie), 1*sizeof(*ie)))) return err;
		if (0 != (err = filesize(&ie[1].offset, jnl->patch_fd))) return err;
	}
	*offset = ie[0].offset;
	assert(ie[1].offset > ie[0].offset);
	*size = ie[1].offset - ie[0].offset;
	return err;
}

static int get_next_version(struct jnl **jnl, long long *next, struct dir *dir, long long version)
{
	// Look for the version number that comes after the given version, and jnl
	STAILQ_FOREACH(*jnl, &dir->jnls, entry) {
		// look first for version+1
		if ((*jnl)->version <= version+1 && (*jnl)->version + (*jnl)->nb_patches > version+1) {
			*next = version+1;
			break;
		} else if ((*jnl)->version > version+1) {	// if we can't find it, skip the gap
			*next = (*jnl)->version;
			break;
		}
	}
	return (*jnl) ? 0:-ENOMSG;
}

static int copy(int dst, int src, off_t offset, size_t size)
{
	char *buf = malloc(size);
	if (! buf) return -ENOMEM;
	int err = 0;
	do {
		if (0 != (err = Read(buf, src, offset, size))) break;
		if (0 != (err = Write(dst, buf, size))) break;
	} while (0);
	free(buf);
	return err;
}

int jnl_send_patch(long long *next_version, struct dir *dir, long long version, int fd)
{
	int err = 0;
	(void)pth_rwlock_acquire(&dir->rwlock, PTH_RWLOCK_RD, FALSE, NULL);
	do {
		off_t patch_offset;
		size_t patch_size;
		struct jnl *jnl;
		if (0 != (err = get_next_version(&jnl, next_version, dir, version))) break;
		if (0 != (err = load_index_entry(&patch_offset, &patch_size, jnl, *next_version - jnl->version))) break;
		// the command (and this line) will be finished by the first line of the patch, which start with "+/-\n"
		char cmdstr[5+1+PATH_MAX+1+20+1+20+1+1];
		size_t cmdlen = snprintf(cmdstr, sizeof(cmdstr), "PATCH /%s %lld %lld ", dir->path + rootdir_len, version, *next_version);
		assert(cmdlen < sizeof(cmdstr));
		if (0 != (err = Write(fd, cmdstr, cmdlen))) break;
		if (0 != (err = copy(fd, jnl->patch_fd, patch_offset, patch_size))) break;
	} while (0);
	(void)pth_rwlock_release(&dir->rwlock);
	return err;
}

/*
 * Directory creation / deletion
 *
 * They are symlinks to id-named directories.
 * User friendly name may be friendly to user, but not to us.
 */

int jnl_createdir(char const *dir, long long dirid, char const *dirname)
{
	int err = 0;
	char diridpath[PATH_MAX];
	char dirlinkpath[PATH_MAX];
	snprintf(diridpath, sizeof(diridpath), "%s/%lld", rootdir, dirid);
	if (0 != (err = Mkdir(diridpath))) return err;	// May already exists
	snprintf(dirlinkpath, sizeof(dirlinkpath), "%s/root/%s/%s", rootdir, dir, dirname);
	if (0 != symlink(diridpath, dirlinkpath)) return -errno;
	return err;
}

int jnl_unlinkdir(char const *dir, char const *dirname)
{
	char dirlinkpath[PATH_MAX];
	snprintf(dirlinkpath, sizeof(dirlinkpath), "%s/root/%s/%s", rootdir, dir, dirname);
	if (0 != unlink(dirlinkpath)) return -errno;
	return 0;
}
