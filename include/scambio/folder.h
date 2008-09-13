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
/*
 * Interface to folders.
 * Beware that many threads may access those files concurrently,
 * although they are non preemptibles.
 */
#ifndef FOLDER_H_080912
#define FOLDER_H_080912

#include <stdbool.h>
#include <unistd.h>	// ssize_t
#include <pth.h>
#include <limits.h>

extern size_t folder_root_len;
extern char folder_root[PATH_MAX];
typedef long long folder_version;
struct folder;
struct header;

int folder_begin(void);
void folder_end(void);

// add/remove a header into a directory
// returns the version after this patch
folder_version folder_patch(struct folder *, enum folder_action, struct header *);

// parent may be NULL to search from root
struct folder *folder_lookup(struct folder *parent, char const *path);

// name is not allowed to use '/' (ie no lookup is performed)
void folder_link_child(struct folder *, char const *name);
int folder_unlink_child(struct folder *, char const *name);

// returns an array of struct folder *
struct folder *folder_children(struct folder *, unsigned *length);
void folder_childern_free(struct folder *);

// returns the header and action (if not NULL) that led to this version
struct header *folder_patch(struct folder *, folder_version, enum folder_action *);
// returns the next version with a patch for this folder
folder_version folder_next_version(struct folder *, folder_version);

#endif
