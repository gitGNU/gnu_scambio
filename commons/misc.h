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
void Write(int fd, void const *buf, size_t len);
void Write_strs(int fd, ...)
#ifdef __GNUC__
	__attribute__ ((sentinel))
#endif
;
void Read(void *buf, int fd, size_t len);
void ReadFrom(void *buf, int fd, off_t offset, size_t len);
void WriteTo(int fd, off_t offset, void const *buf, size_t len);
void Copy(int dest, int src);
void Mkdir(char const *path);
void Mkdir_for_file(char const *path);
void Make_path(char *buf, size_t bufsize, ...)
#ifdef __GNUC__
	__attribute__ ((sentinel))
#endif
;
// a line is said to match a delim if it starts with the delim, and is followed only by optional spaces
bool line_match(char *restrict line, char *restrict delim);

void path_push(char path[], char const *next);
void path_pop(char path[]);
 
#include <sys/types.h>
off_t filesize(int fd);

char *Strdup(char const *orig);
void FreeIfSet(char **ptr);
char const *Basename(char const *path);

int Connect(char const *host, char const *service);

#endif
