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
#ifndef C2L_H_081125
#define C2L_H_081125
#include "mdsyncc.h"

struct c2l_map {
	LIST_ENTRY(c2l_map) entry;
	mdir_version central, local;
};

struct c2l_map *c2l_new(struct c2l_maps *list, mdir_version central, mdir_version local);
void c2l_del(struct c2l_map *c2l);
struct c2l_map *c2l_search(struct c2l_maps *list, mdir_version central);

#endif
