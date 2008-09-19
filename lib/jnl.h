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
#ifndef JNL_H_080916
#define JNL_H_080916

#include <stdbool.h>
#include "scambio/mdir.h"
#include "scambio/queue.h"

struct header;

// jnl and mdir are both used by jnl.c and mdir.c

struct jnl {
	STAILQ_ENTRY(jnl) entry;
	int idx_fd;	// to the index file
	int patch_fd;	// to the patch file
	mdir_version version;	// version of the first patch in this journal
	unsigned nb_patches;	// number of patches in this file
	struct mdir *mdir;
};

struct mdir {
	LIST_ENTRY(mdir) entry;	// entry in the list of cached dirs
	STAILQ_HEAD(jnls, jnl) jnls;	// list all jnl in this directory (refreshed from time to time), ordered by first_version
	LIST_HEAD(mdir_listeners, mdir_listener) listeners;	// call them when a patch is appended
	pth_rwlock_t rwlock;
	char path[PATH_MAX];	// absolute path to the dir (actual one, not one of the symlinks)
};

void jnl_begin(void);
void jnl_end(void);
struct jnl *jnl_new(struct mdir *mdir, char const *filename);
void jnl_del(struct jnl *);
struct jnl *jnl_new_empty(struct mdir *mdir, mdir_version starting_version);
bool jnl_too_big(struct jnl *);
mdir_version jnl_patch(struct jnl *, enum mdir_action, struct header *);
struct header *jnl_read(struct jnl *, unsigned index, enum mdir_action *action);
bool is_jnl_file(char const *filename);

#endif
