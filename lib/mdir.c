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
#include "scambio/header.h"
#include "scambio/mdir.h"
#include "varbuf.h"
#include "log.h"
#include "misc.h"
#include "digest.h"
#include "persist.h"

/*
 * Data Definitions
 *
 */

struct jnl;
struct mdir {
	LIST_ENTRY(mdir) entry;	// entry in the list of cached dirs (FIXME: hash this!)
	STAILQ_HEAD(jnls, jnl) jnls;	// list all jnl in this directory (refreshed from time to time), ordered by first_version
	dev_t st_dev;	// st_dev and st_ino are used to identify directories
	ino_t st_ino;
	pth_rwlock_t rwlock;
	char path[PATH_MAX];	// absolute path to the dir (actual one, not one of the symlinks)
};

static size_t mdir_root_len;
static char const *mdir_root;
static LIST_HEAD(mdirs, mdir) mdirs;
static struct persist dirid_seq;

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
	struct stat statbuf;
	if (0 != stat(mdir->path, &statbuf)) with_error(errno, "stat %s", mdir->path) return;
	mdir->st_dev = statbuf.st_dev;
	mdir->st_ino = statbuf.st_ino;
	(void)pth_rwlock_init(&mdir->rwlock);	// FIXME: if this can schedule some other thread, we need to prevent addition of the same dir twice
	STAILQ_INIT(&mdir->jnls);
	// Find journals
	DIR *d = opendir(mdir->path);
	if (! d) with_error(errno, "opendir %s", mdir->path) return;
	struct dirent *dirent;
	// We may yield the CPU here, but then we must acquire write lock
	while (NULL != (dirent = readdir(d))) {
		if (0 == strncmp("jnl", dirent->d_name, 3)) {
			jnl_new(mdir, dirent->d_name);
			on_error break;
		}
	}
	if (0 != closedir(d)) error_push(errno, "Cannot closedir %s", mdir->path);
	unless_error LIST_INSERT_HEAD(&mdirs, mdir, entry);
}

static struct mdir *mdir_new(char const *id, bool create)
{
	struct mdir *mdir = malloc(sizeof(*mdir));
	if (! mdir) with_error(ENOMEM, "malloc mdir") return NULL;
	mdir_ctor(mdir, id, create);
	on_error {
		free(mdir);
		return NULL;
	}
	return mdir;
}

struct mdir *mdir_create(void)
{
	uint64_t id = (*(uint64_t *)dirid_seq.data)++;
	char id_str[20+1];
	snprintf(id_str, sizeof(id_str), "%"PRIu64, id);
	return mdir_new(id_str, true);
}

/*
 * lookup
 */

struct mdir *mdir_lookup(char const *name)
{
	// OK, so we have a name. We let the kernel do the lookup for us (symlinks),
	// then fetch the id from the result.
	char path[PATH_MAX];
	char slink[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", mdir_root, name);
	ssize_t len = readlink(path, slink, sizeof(slink));
	if (len == -1) with_error(errno, "readlink %s", path) return NULL;
	if (len == 0 || len == sizeof(slink)) with_error(0, "bad symlink for %s", path) return NULL;
	char *id = slink + len - 1;
	while (id >= slink && *id != '/') id--;
	if (id >= slink) *id = '\0';
	return mdir_new(id+1, false);
}

/*
 * Init
 */

void mdir_begin(void)
{
	// Default configuration values
	conf_set_default_str("MDIR_ROOT_DIR", "/tmp/mdir");
	conf_set_default_str("MDIR_DIRSEQ", "/tmp/.dirid.seq");
	on_error return;
	// Inits
	LIST_INIT(&mdirs);
	mdir_root = conf_get_str("MDIR_ROOT_DIR");
	mdir_root_len = strlen(mdir_root);
	persist_ctor(&dirid_seq, sizeof(uint64_t), conf_get_str("MDIR_DIRSEQ"));
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
	free(mdir);
}

void mdir_end(void)
{
	struct mdir *mdir;
	while (NULL != (mdir = LIST_FIRST(&mdirs))) {
		mdir_del(mdir);
	}
	persist_dtor(&dirid_seq);
}

/*
 * (un)link
 */

void mdir_link(struct mdir *parent, char const *name, struct mdir *child)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", parent->path, name);
	if (0 != symlink(child->path, path)) error_push(errno, "symlink %s -> %s", path, child->path);
}

void mdir_unlink(struct mdir *parent, char const *name)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", parent->path, name);
	if (0 != unlink(path)) error_push(errno, "unlink %s", path);
}

/*
 * read
 */

struct header *mdir_read_next(struct mdir *, mdir_version, enum mdir_action *);

/*
 * patch a mdir
 */

void mdir_patch(struct mdir *, enum mdir_action, struct header *);

/*
 * accessors
 */

mdir_version mdir_last_version(struct mdir *);
char const *mdir_id(struct mdir *);
char const *mdir_name(struct mdir *);
char const *mdir_key(struct mdir *);

