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
#ifndef MISC_H_080625
#define MISC_H_080625

#include <stddef.h>	// size_t (? FIXME)
#include <stdbool.h>
int Write(int fd, void const *buf, size_t len);
int Read(void *buf, int fd, off_t offset, size_t len);
int Mkdir(char const *path);
// a line is said to match a delim if it starts with the delim, and is followed only by optional spaces
bool line_match(char *restrict line, char *restrict delim);

void path_push(char path[], char const *next);
void path_pop(char path[]);

#endif
