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
/* Persistant data, ie a struct that's also present on a disk file.
 * Aka very simple wrapper to mmap.
 */
#ifndef PERSIST_H_080628
#define PERSIST_H_080628

#include <inttypes.h>

struct persist {
	size_t size;
	void *data;
	int fd;
};

void persist_ctor(struct persist *, size_t size, char const *fname, void const *default_value);
void persist_dtor(struct persist *);
void persist_ctor_sequence(struct persist *p, char const *fname, uint64_t default_value);
void persist_lock(struct persist *);
void persist_unlock(struct persist *);
static inline void const *persist_read(struct persist *persist)
{
	return persist->data;
}

void persist_write(struct persist *, void *);
uint64_t persist_read_inc_sequence(struct persist *);

#endif
