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
/* Interface to read and write journals and snapshots.
 * Beware that many threads may access those files concurrently,
 * although they are non preemptibles.
 */
#ifndef JNL_H_080623
#define JNL_H_080623

#include <stdbool.h>
#include <unistd.h>	// ssize_t
#include <pth.h>
#include "queue.h"
#include "mdird.h"

struct dir;
struct jnl;
struct stribution;

// max size is given in number of versions
int jnl_begin(void);
void jnl_end(void);

// Add an header into a directory
struct header;
int jnl_add_patch(char const *path, char action, struct header *header);
// version is the version we want to patch
int jnl_send_patch(long long *actual_version, struct dir *dir, long long version, int fd);

int dir_get(struct dir **dir, char const *path);
int dir_exist(char const *path);
int strib_get(struct stribution **, char const *path);

bool dir_same_path(struct dir *dir, char const *path);
long long dir_last_version(struct dir *dir);
void dir_register_subscription(struct dir *dir, struct subscription *sub);

int jnl_createdir(char const *dir, long long dirid, char const *dirname);
int jnl_unlinkdir(char const *dir, char const *dirname);

#endif
