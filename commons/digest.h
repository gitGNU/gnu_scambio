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
#ifndef DIGEST_H_080725
#define DIGEST_H_080725

#define MAX_DIGEST_STRLEN 64	// FIXME: CHECKME
#define MAX_DIGEST_LEN 128
#include <stddef.h> // size_t

/* Compute a message digest based on the header. Make it long enought to be
 * discriminent, but not too much that it still fit on this fixed sized array.
 * Also, you are not allowed to return with an error. Go ahead, Dilbert !
 * Returns the actual length of the string.
 */
size_t digest(char *out, size_t len, char const *in);

/* This one may report an error.
 */
size_t digest_file(char *out, char const *filename);

#endif
