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
#ifndef SUB_H_080627
#define SUB_H_080627

#include <stdbool.h>
#include <unistd.h>	// ssize_t
#include <pth.h>
#include "scambio/queue.h"
#include "scambio/mdir.h"

struct mdir;

/* Beware that this structure, being in two very different lists, may be changed
 * by THREE different threads :
 * - the one that handle the client requests,
 * - the one that handle a client subscription,
 * - and any other one writing in this directory.
 * Use directory mutex for safety !
 */

struct cnx_env;
struct subscription {
	// Managed by client thread
	LIST_ENTRY(subscription) env_entry;	// in the client list of subscriptions.
	struct cnx_env *env;
	pth_t thread_id;
	// Managed by JNL module from here
	struct mdir *mdir;
	mdir_version version;	// last known version (updated when we send a patch)
	struct mdir_listener listener;
	bool registered;	// is the listener registered to the mdir ?
	pth_msgport_t msg_port;
};

struct subscription *subscription_new(struct cnx_env *env, char const *name, mdir_version version);
void subscription_del(struct subscription *sub);
struct subscription *subscription_find(struct cnx_env *env, char const *path);
void subscription_reset_version(struct subscription *sub, mdir_version version);

#endif
