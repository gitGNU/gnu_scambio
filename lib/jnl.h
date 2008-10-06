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

void jnl_begin(void);
void jnl_end(void);
struct jnl *jnl_new(struct mdir *mdir, char const *filename);
void jnl_del(struct jnl *);
struct jnl *jnl_new_empty(struct mdir *mdir, mdir_version starting_version);
bool jnl_too_big(struct jnl *);
mdir_version jnl_patch_add(struct jnl *, struct header *);
mdir_version jnl_patch_del(struct jnl *, mdir_version to_del);
void jnl_mark_del(struct jnl *, mdir_version to_del);
bool jnl_read(struct jnl *jnl, unsigned index, struct header **header, enum mdir_action *action);
bool is_jnl_file(char const *filename);

#endif
