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
#ifndef MDIR_H_080708
#define MDIR_H_080708

/* Various tools to deal with a mdir tree */

#include <limits.h>
extern size_t mdir_root_len;
extern char mdir_root[PATH_MAX];

int mdir_begin(void);
void mdir_end(void);

#include <stdbool.h>
bool mdir_folder_exists(char const *folder);

#endif
