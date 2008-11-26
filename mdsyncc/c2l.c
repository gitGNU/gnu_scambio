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
#include "scambio.h"
#include "scambio/mdir.h"
#include "c2l.h"
#include "mdsyncc.h"

struct c2l_map *c2l_new(struct c2l_maps *list, mdir_version central, mdir_version local)
{
	debug("local version %"PRIversion" corresponds to central version %"PRIversion, local, central);
	struct c2l_map *c2l = malloc(sizeof(*c2l));
	if (! c2l) with_error(ENOMEM, "malloc(c2l)") return NULL;
	c2l->central = central;
	c2l->local = local;
	LIST_INSERT_HEAD(list, c2l, entry);
	return c2l;
}

void c2l_del(struct c2l_map *c2l)
{
	LIST_REMOVE(c2l, entry);
	free(c2l);
}

struct c2l_map *c2l_search(struct c2l_maps *list, mdir_version central)
{
	struct c2l_map *c2l;
	LIST_FOREACH(c2l, list, entry) {
		if (c2l->central == central) return c2l;
	}
	return NULL;
}
