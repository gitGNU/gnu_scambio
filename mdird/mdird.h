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
#ifndef MDIRD_H_080623
#define MDIRD_H_080623

#include <pth.h>
#include "scambio/mdir.h"

struct subscription;
struct cnx_env {
	int fd;
	LIST_HEAD(subscriptions, subscription) subscriptions;
	pth_mutex_t wfd;	// protects fd on write
};

void exec_begin(void);
void exec_end(void);
void exec_sub  (struct cnx_env *, long long seq, char const *dir, mdir_version version);
void exec_unsub(struct cnx_env *, long long seq, char const *dir);
void exec_put  (struct cnx_env *, long long seq, char const *dir);
void exec_rem  (struct cnx_env *, long long seq, char const *dir);
void exec_quit (struct cnx_env *, long long seq);

#endif
