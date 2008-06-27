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

/*
 * Data Definitions
 *
 * We have some cache here. Beware they keep coherency at pth
 * scheduler point.
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
	if (0 != (err = Mkdir(path))) return err;
	if (0 != (err = stat(dir->path, &statbuf))) return err;
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
	snprintf(abspath, sizeof(abspath), "%s/%s", rootdir, path);
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
	if (0 != (err = conf_set_default_str("SCAMBIO_ROOT_DIR", "/tmp"))) return err;
	if (0 != (err = conf_set_default_int("SCAMBIO_MAX_JNL_SIZE", 2000))) return err;
	if (0 != (err = conf_set_default_str("SCAMBIO_STRIB_FNAME", ".stribution.conf"))) return err;
	// Inits
	LIST_INIT(&dirs);
	rootdir = conf_get_str("SCAMBIO_ROOT_DIR");
	rootdir_len = strlen(rootdir);
	max_jnl_size = conf_get_int("SCAMBIO_MAX_JNL_SIZE");
	strib_fname = conf_get_str("SCAMBIO_STRIB_FNAME");
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
	size_t path_len = snprintf(abspath, sizeof(abspath), "%s/%s", rootdir, path);
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
	// TODO: delete some dir from time to time
	if (0 != (err = dir_new(dir, path_len, abspath))) return err;
	return 0;
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
	long long version = jnl->version + jnl->nb_patches;
	struct index_entry ie;
	if (0 != (err = filesize(&ie.offset, jnl->patch_fd))) return err;
	// Write the index
	if (0 != (err = Write(jnl->idx_fd, &ie, sizeof(ie)))) return err;
	// Then the patch
	if (0 != (err = header_write(header, jnl->patch_fd))) return err;
	char action_str[32];
	size_t const len = snprintf(action_str, sizeof(action_str), "%%%c %lld\n", action, version);	// TODO: add ctime ?
	assert(len < sizeof(action_str));
	if (0 != (err = Write(jnl->patch_fd, action_str, len))) return err;
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

int jnl_send_patch(long long *actual_version, struct dir *dir, long long version, int fd)
{
	int err = 0;
	(void)pth_rwlock_acquire(&dir->rwlock, PTH_RWLOCK_RD, FALSE, NULL);
	// FIXME: split en plusieurs fonctions
	// Look for the version number that comes after the given version, and jnl
	struct jnl *jnl;
	STAILQ_FOREACH(jnl, &dir->jnls, entry) {
		// look first for version+1
		if (jnl->version <= version+1 && jnl->version + jnl->nb_patches > version+1) {
			*actual_version = version+1;
			break;
		} else if (jnl->version > version+1) {	// if we can't find it, skip the gap
			*actual_version = jnl->version;
			break;
		}
	}
	if (! jnl) err = -ENOMSG;
	if (! err) { // Read the patch
		off_t patch_offset;
		size_t patch_size;
		err = load_index_entry(&patch_offset, &patch_size, jnl, *actual_version - jnl->version);
		if (! err) {	// Copy from file to socket
			struct varbuf vb;
			char cmdstr[5+1+PATH_MAX+1+20+1];
			size_t cmdlen = snprintf(cmdstr, sizeof(cmdstr), "PATCH /%s %lld\n", dir->path + rootdir_len, version);
			assert(cmdlen < sizeof(cmdstr)-1);
			err = varbuf_ctor(&vb, cmdlen + patch_size, true);
			if (! err) {
				err = varbuf_append(&vb, cmdlen, cmdstr);
				char *buf = vb.buf + vb.used;
				if (! err) err = varbuf_put(&vb, patch_size);
				if (! err) err = Read(buf, jnl->patch_fd, patch_offset, patch_size);
				if (! err) err = Write(fd, vb.buf, vb.used);
				varbuf_dtor(&vb);
			}
		}
	}
	(void)pth_rwlock_release(&dir->rwlock);
	return err;
}

