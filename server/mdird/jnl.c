#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include "scambio.h"
#include "log.h"
#include "jnl.h"

/*
 * Data Definitions
 *
 * We have some cache here. Beware they keep coherency at pth
 * scheduler point.
 */

static char const *rootdir;

struct dir {
	LIST_ENTRY(dir) entry;	// entry in the list of cached dirs (FIXME: hash this!)
	STAILQ_HEAD(jnls, jnl) jnls;	// list all jnl in this directory (refreshed from time to time), ordered by first_version
	char path[PATH_MAX];
	size_t path_len;
	int count;	// number of jnl_find using this dir
};

static LIST_HEAD(dirs, dir) dirs;

/*
 * Jnls
 */

static int jnl_ctor(struct jnl *jnl, struct dir *dir, char const *filename)
{
	int err = 0;
	if (0 != parse_version(filename, &jnl->first_version, &jnl->last_version)) return -EINVAL;
	size_t len = snprintf(dir->path+dir->path_len, sizeof(dir->path)-dir->path_len, "/%s", filename);
	if (len < sizeof(dir->path)-dir->path_len) {
		err = -ENAMETOOLONG;
	} else if (0 > (jnl->fd = open(dir->path, O_APPEND))) {
		err = -errno;
	} else {	// All is OK, insert it as a journal
		if (STAILQ_EMPTY(&dir->jnl)) {
			STAILQ_INSERT_HEAD(&dir->jnl, jnl, entry);
		} else {	// insert in first_version order
			struct jnl **prevptr = &dir->jnls.stqh_first;
			while (prevptr != dir->jnls.stqh_last) {
				struct jnl *j = *prevptr;
				if (j->first_version > jnl->first_version) break;
			}
			STAILQ_INSERT_AFTER(&dir->jnls, *prevptr, jnl, entry);
		}
	}
	dir->path[dir->path_len] = '\0';
	return err;
}

static void jnl_dtor(struct jnl *jnl, struct dir *dir)
{
	STAILQ_REMOVE(&dir->jnl, jnl, jnl, entry);
	if (0 != close(jnl->fd)) {
		error("Cannot close journal for %lld->%lld in '%s'", jnl->first_version, jnl->last_version, dir->path);
	}
}

static jnl *jnl_new(struct dir *dir, char const *filename)
{
	struct jnl *jnl = malloc(sizeof(*jnl));
	if (jnl && 0 != jnl_ctor(jnl, dir, filename)) {
		free(jnl);
		jnl = NULL;
	}
	return jnl;
}

/*
 * Cached dir
 */

static int dir_ctor(struct dir *dir, char const *path)
{
	int err = 0;
	dir->path_len = snprintf(dir->path, sizeof(dir->path), "%s/%s", rootdir, path);
	if (dir->path_len >= sizeof(dir->path)) return -ENAMETOOLONG;
	while (dir->path_len > 1 && dir->path[dir->path_len-1] == '/') {	// removes unnecessary slashes
		dir->path[--dir->path_len] = '\0';
	}
	if (strstr(path, "/../")) return -EACCES;
	dir->count = 0;
	STAILQ_INIT(&dir->jnls);
	// Find journals
	DIR *d = opendir(path);
	if (! d) return -errno;
	struct dirent *dirent;
	while (NULL != (dirent = readdir(d))) {
		int err;
		if (0 != (err = jnl_new(dir, dirent->d_name))) {
			warning("Cannot add journal '%s/%s' : %s", dir->path, dirent->d_name, strerror(-err));
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
}

static struct dir *dir_new(char const *path)
{
	struct dir *dir = malloc(sizeof(*dir));
	if (dir && 0 != dir_ctor(dir, path)) {
		free(dir);
		dir = NULL;
	}
	return dir;
}

static void dir_del(struct dir *dir)
{
	dir_dtor(dir);
	free(dir);
}

/*
 * (De)Initialization
 */

int jnl_begin(char const *rootdir_)
{
	LIST_INIT(&dirs);
	rootdir = strdup(rootdir);
}

void jnl_end(void)
{
	struct dir *dir;
	while (NULL != (dir = LIST_FIRST(&dirs))) {
		dir_del(dir);
	}
	free(rootdir);
}

