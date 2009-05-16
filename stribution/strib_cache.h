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
#ifndef CONF_CACHE_H_080603
#define CONF_CACHE_H_080603

#include <time.h>
#include <limits.h>
#include "scambio.h"
#include "stribution.h"

struct strib_cached {
	struct stribution *stribution;	// variable size so pointed, but 1 to 1 relation
	LIST_ENTRY(strib_cached) entry;
	time_t last_used;
	char path[PATH_MAX];
};

int strib_cache_begin(char const *root_path);
void strib_cache_end(void);
struct stribution *strib_cache_get(char const *path);

#endif
