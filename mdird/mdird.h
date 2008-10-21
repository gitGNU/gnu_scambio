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

#include <stddef.h>
#include <pth.h>
#include "scambio/mdir.h"
#include "scambio/cnx.h"
#include "auth.h"

struct subscription;
struct cnx_env {
	struct mdir_cnx cnx;
	bool quit;
	LIST_HEAD(subscriptions, subscription) subscriptions;
	pth_mutex_t wfd;	// protects fd on write (put that on mdir_cnx ?
};

struct mdird {
	struct mdir mdir;
	struct subscriptions subscriptions;
};

static inline struct mdird *mdir2mdird(struct mdir *mdir)
{
	return (struct mdird *)((char *)mdir - offsetof(struct mdird, mdir));
}

void exec_begin(void);
void exec_end(void);
extern mdir_cmd_cb exec_quit, exec_sub, exec_unsub, exec_put, exec_rem, exec_auth;

#endif
