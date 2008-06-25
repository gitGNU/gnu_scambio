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

/*
 * Data Definitions
 *
 * We have some cache here. Beware they keep coherency at pth
 * scheduler point.
 */

static char *rootdir;
static unsigned max_jnl_size;

struct dir {
	LIST_ENTRY(dir) entry;	// entry in the list of cached dirs (FIXME: hash this!)
	STAILQ_HEAD(jnls, jnl) jnls;	// list all jnl in this directory (refreshed from time to time), ordered by first_version
	dev_t st_dev;	// st_dev and st_ino are used to identify directories
	ino_t st_ino;
	int count;	// number of jnl_find using this dir
	pth_mutex_t mutex;
	size_t path_len;
	char path[PATH_MAX];
};

static LIST_HEAD(dirs, dir) dirs;

/*
 * Jnls
 */

#define JNL_FILE_FORMAT "%lld_%lld.jnl"
#define JNL_FILE_FIRST "0_0.jnl"

#define PRIjnl "s/%lld_%lld.jnl"
#define PRIJNL(j) (j)->dir->path, (j)->first_version, (j)->last_version

static int parse_version(char const *filename, long long *first, long long *last)
{
	if (2 != sscanf(filename, JNL_FILE_FORMAT, first, last)) {	// as we try any file in the directorym this will happen
		debug("'%s' is not a journal file", filename);
		return -1;
	}
	if (*first > *last) {
		error("Bad journal versions : (first, last) = (%lld, %lld)", *first, *last);
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

static int jnl_ctor(struct jnl *jnl, struct dir *dir, char const *filename)
{
	int err = 0;
	if (0 != parse_version(filename, &jnl->first_version, &jnl->last_version)) return -EINVAL;
	if (0 != (err = append_to_dir_path(dir, filename))) return err;
	if (0 > (jnl->fd = open(dir->path, O_RDWR|O_APPEND|O_CREAT, 0660))) {
		err = -errno;
	} else {	// All is OK, insert it as a journal
		if (STAILQ_EMPTY(&dir->jnls)) {
			STAILQ_INSERT_HEAD(&dir->jnls, jnl, entry);
		} else {	// insert in first_version order
			struct jnl *j, *prev = NULL;
			STAILQ_FOREACH(j, &dir->jnls, entry) {
				assert(j->first_version != jnl->first_version);
				if (j->first_version > jnl->first_version) break;
				prev = j;
			}
			if (prev) {
				STAILQ_INSERT_AFTER(&dir->jnls, prev, jnl, entry);
			} else {
				STAILQ_INSERT_HEAD(&dir->jnls, jnl, entry);
			}
		}
		jnl->dir = dir;
	}
	restore_dir_path(dir);
	return err;
}

static void jnl_dtor(struct jnl *jnl)
{
	STAILQ_REMOVE(&jnl->dir->jnls, jnl, jnl, entry);
	if (0 != close(jnl->fd)) {
		error("Cannot close journal %"PRIjnl, PRIJNL(jnl));
	}
}

static struct jnl *jnl_new(struct dir *dir, char const *filename)
{
	struct jnl *jnl = malloc(sizeof(*jnl));
	if (jnl && 0 != jnl_ctor(jnl, dir, filename)) {
		free(jnl);
		jnl = NULL;
	}
	return jnl;
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
	dir->path_len = path_len;
	memcpy(dir->path, path, path_len+1);
	struct stat statbuf;
	if (0 != (err = stat(dir->path, &statbuf))) return err;
	dir->st_dev = statbuf.st_dev;
	dir->st_ino = statbuf.st_ino;
	dir->count = 0;
	(void)pth_mutex_init(&dir->mutex);	// always returns 0
	STAILQ_INIT(&dir->jnls);
	// Find journals
	DIR *d = opendir(dir->path);
	if (! d) return -errno;
	struct dirent *dirent;
	errno = 0;
	while (NULL != (dirent = readdir(d))) {
		if (! jnl_new(dir, dirent->d_name)) {
			warning("Cannot add journal '%s/%s'", dir->path, dirent->d_name);
		}
	}
	if (errno) err = -errno;
	if (0 != closedir(d)) error("Cannot closedir('%s') : %s", dir->path, strerror(errno));	// ignore this
	LIST_INSERT_HEAD(&dirs, dir, entry);
	return err;
}

static void dir_dtor(struct dir *dir)
{
	assert(dir->count == 0);
	struct jnl *jnl;
	while (NULL != (jnl = STAILQ_FIRST(&dir->jnls))) {
		jnl_del(jnl);
	}
	LIST_REMOVE(dir, entry);
	pth_mutex_release(&dir->mutex);
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

/*
 * (De)Initialization
 */

int jnl_begin(char const *rootdir_, unsigned max_jnl_size_)
{
	LIST_INIT(&dirs);
	rootdir = strdup(rootdir_);
	max_jnl_size = max_jnl_size_;
	return 0;
}

void jnl_end(void)
{
	struct dir *dir;
	while (NULL != (dir = LIST_FIRST(&dirs))) {
		dir_del(dir);
	}
	free(rootdir);
}

/*
 * Searching for a version
 */

static int dir_get(struct dir **dir, char const *path)
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
	// FIXME: delete some unused old dir maybe ?
	return dir_new(dir, path_len, abspath);
}

static ssize_t jnl_offset_of(struct jnl *jnl, long long version)
{
	// We use our own on-stack offset for scanning the file to achieve reentrance with same jnl
	struct varbuf vb;
	int err = 0;
	if (0 != (err = varbuf_ctor(&vb, MAX_HEADLINE_LENGTH, true))) return err;
	char *line;
	off_t offset = 0, new_offset;
	while (0 <= (new_offset = varbuf_read_line_off(&vb, jnl->fd, MAX_HEADLINE_LENGTH, offset, &line))) {
		char action;
		long long this_version;
		if (2 == sscanf(line, "%%%c %lld", &action, &this_version)) {
			if (action != '+' && action != '-') {
				error("Invalid action in journal %"PRIjnl" at version %lld : %c", PRIJNL(jnl), this_version, action);
				err = -EINVAL;
				break;
			}
			if (this_version > version) {
				error("Cannot find version %lld in jounral %"PRIjnl, version, PRIJNL(jnl));
				err = -ENOMSG;
				break;
			}
			if (version == this_version) {
				debug("Found version %lld at offset %llu", version, (unsigned long long)offset);
				err = offset;
				break;
			}
			debug("Skip version %lld (offset %llu)", this_version, (unsigned long long) offset);
		}
		if (new_offset == offset) {	// EOF
			// We should not reach EOF without having found what we are looking for : complains loudly
			error("Cannot find version %lld in journal %"PRIjnl" (up to offset %llu)",
				version, PRIJNL(jnl), (unsigned long long)offset);
			err = -ENOENT;
			break;
		}
		offset = new_offset;
	}
	varbuf_dtor(&vb);
	return err;
}

ssize_t jnl_find(struct jnl **jnlp, char const *path, long long version)
{
	int err = 0;
	struct dir *dir;
	if (0 != (err = dir_get(&dir, path))) return err;
	struct jnl *jnl = STAILQ_FIRST(&dir->jnls);
	if (! jnl) return -ENOMSG;
	if (version < jnl->first_version) version = jnl->first_version;
	jnl = STAILQ_LAST(&dir->jnls, jnl, entry);
	if (version > jnl->last_version) return -ENOMSG;
	STAILQ_FOREACH(jnl, &dir->jnls, entry) {
		if (jnl->first_version <= version && jnl->last_version >= version) {
			if (jnlp) {
				*jnlp = jnl;
				dir->count ++;
			}
			return jnl_offset_of(jnl, version);
		}
	}
	return -ENOMSG;
}

void jnl_release(struct jnl *jnl)
{
	jnl->dir->count --;
}

int jnl_copy(struct jnl *jnl, off_t offset, int fd)
{
	int err = 0;
	char copybuf[1024];
	do {
		ssize_t inbytes = pth_pread(jnl->fd, copybuf, sizeof(copybuf), offset);
		if (inbytes < 0) {
			if (errno == EINTR) continue;
			err = -errno;
			break;
		} else if (inbytes == 0) break;
		offset += inbytes;
		err = Write(fd, copybuf, inbytes);
	} while (1);
	return err;
}

/*
 * Append an action into the journal.
 * The write itself is (supposed to be) atomic, but may schedule another thread
 * *before* the write syscall is started. The scheduled thread may also want to add a message.
 * We then need a mutex on this directory.
 */

// FIXME: on error, truncate the file so its unchanged !
static int write_action(struct header *header, char action, long long version, int fd)
{
	int err = 0;
	char action_str[32];
	size_t const len = snprintf(action_str, sizeof(action_str), "%%%c %lld\n", action, version);
	assert(len < sizeof(action_str));
	if (0 != (err = Write(fd, action_str, len))) return err;
	return header_write(header, fd);
}

// first action of a new journal, starting at given version
static int add_action_new_jnl(struct dir *dir, char action, struct header *header, long long version)
{
	int err = 0;
	char filename[PATH_MAX];
	(void)snprintf(filename, sizeof(filename), JNL_FILE_FORMAT, version, version);
	struct jnl *jnl = jnl_new(dir, filename);
	if (! jnl) return -1;	// FIXME
	if (0 != (err = write_action(header, action, version, jnl->fd))) {
		jnl_del(jnl);
		return err;
	}
	return 0;
}

static int add_action_to_jnl(struct jnl *jnl, char action, struct header *header)
{
	int err;
	if (0 != (err = write_action(header, action, jnl->last_version+1, jnl->fd))) return err;
	// Rename
	char oldname[PATH_MAX];
	char newname[PATH_MAX];
	snprintf(oldname, sizeof(oldname), "%s/"JNL_FILE_FORMAT, jnl->dir->path, jnl->first_version, jnl->last_version);
	jnl->last_version++;
	snprintf(newname, sizeof(newname), "%s/"JNL_FILE_FORMAT, jnl->dir->path, jnl->first_version, jnl->last_version);
	if (0 != rename(oldname, newname)) return -errno;
	return 0;
}

static unsigned jnl_size(struct jnl *jnl)
{
	assert(jnl->last_version >= jnl->first_version);
	return jnl->last_version - jnl->first_version;
}

int jnl_add_action(char const *path, char action, struct header *header)
{
	int err = 0;
	struct dir *dir;
	if (0 != (err = dir_get(&dir, path))) return err;
	(void)pth_mutex_acquire(&dir->mutex, FALSE, NULL);
	struct jnl *jnl = STAILQ_LAST(&dir->jnls, jnl, entry);
	if (! jnl) {
		err = add_action_new_jnl(dir, action, header, 0);
	} else if (jnl_size(jnl) > max_jnl_size) {
		err = add_action_new_jnl(dir, action, header, jnl->last_version+1);
	} else {
		err = add_action_to_jnl(jnl, action, header);
	}
	(void)pth_mutex_release(&dir->mutex);
	return err;
}

