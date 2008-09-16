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
 * We have some cache here. Beware that they keep coherency at pth scheduling points.
 */

size_t mdir_root_len;
char mdir_root[PATH_MAX];

static LIST_HEAD(mdirs, mdir) mdirs;

static char const key_field[] = "scambio-key";

struct jnl;
struct mdir {
	LIST_ENTRY(mdir) entry;	// entry in the list of cached dirs (FIXME: hash this!)
	STAILQ_HEAD(jnls, jnl) jnls;	// list all jnl in this directory (refreshed from time to time), ordered by first_version
	dev_t st_dev;	// st_dev and st_ino are used to identify directories
	ino_t st_ino;
	pth_rwlock_t rwlock;
	size_t path_len;
	char path[PATH_MAX];	// absolute path to the directory, to the actual directory (not the links to it)
	char id[20+1];
};

/*
 * Init
 */

struct persist dirid_seq;

void mdir_begin(void)
{
	conf_set_default_str("MDIR_DIRSEQ", "/tmp/.dirid.seq");
	persist_ctor(&dirid_seq, sizeof(long long), conf_get_str("MDIR_DIRSEQ"));
}

void mdir_end(void)
{
	persist_dtor(&dirid_seq);
}

/*
 * mdir creation
 */


struct mdir *mdir_new(void)
{
//	uint64_t id = (*(long long *)dirid_seq.data)++;
//	snprintf(mdir->id, sizeof(mdir->id), "%"PRIu64, id);
	return NULL;
}

/*
 * Patch a mdir
 */

void mdir_patch(struct mdir *, enum mdir_action, struct header *);

/*
 * mdir lookup
 */

struct mdir *mdir_lookup(char const *path);

/*
 * (un)link
 */

void mdir_link(struct mdir *parent, char const *name, struct mdir *child);
void mdir_unlink(struct mdir *parent, char const *name);

/*
 * Read
 */

struct header *mdir_read(struct mdir *, mdir_version, enum mdir_action *);

/*
 * Cursor
 */

mdir_version mdir_next_version(struct mdir *, mdir_version);

