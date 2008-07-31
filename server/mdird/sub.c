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
#include <stdbool.h>
#include "scambio.h"
#include "varbuf.h"
#include "jnl.h"
#include "sub.h"

/*
 * Creation / Deletion / Reset
 */

static void *subscription_thread(void *sub);

static int subscription_ctor(struct subscription *sub, struct cnx_env *env, char const *path, long long version)
{
	int err = 0;
	sub->version = version;
	sub->env = env;
	err = dir_get(&sub->dir, path);
	if (! err) {
		LIST_INSERT_HEAD(&env->subscriptions, sub, env_entry);
		subscription_reset_version(sub, version);
		dir_register_subscription(sub->dir, sub);
		sub->thread_id = pth_spawn(PTH_ATTR_DEFAULT, subscription_thread, sub);
	}
	return err;
}

int subscription_new(struct subscription **sub, struct cnx_env *env, char const *path, long long version)
{
	int err = 0;
	*sub = malloc(sizeof(**sub));
	if (*sub && 0 != (err = subscription_ctor(*sub, env, path, version))) {
		free(*sub);
		*sub = NULL;
	}
	return err;
}

static void subscription_dtor(struct subscription *sub)
{
	LIST_REMOVE(sub, dir_entry);
	LIST_REMOVE(sub, env_entry);
}

void subscription_del(struct subscription *sub)
{
	subscription_dtor(sub);
	free(sub);
}

void subscription_reset_version(struct subscription *sub, long long version)
{
	if (version > sub->version) return;	// forget about it
	sub->version = version;
}

/*
 * Find
 */

struct subscription *subscription_find(struct cnx_env *env, char const *dir)
{
	struct subscription *sub;
	LIST_FOREACH(sub, &env->subscriptions, env_entry) {
		if (dir_same_path(sub->dir, dir)) return sub;
	}
	return NULL;
}

/*
 * Thread
 */

static bool client_needs_patch(struct subscription *sub)
{
	return sub->version < dir_last_version(sub->dir);
}

static int send_next_patch(struct subscription *sub)
{
	long long sent_version;
	int err = jnl_send_patch(&sent_version, sub->dir, sub->version, sub->env->fd);
	if (! err) sub->version = sent_version;	// last version known is the last we sent
	return err;
}

static void *subscription_thread(void *sub_)
{
	// FIXME: we need a pointer from subscription to the client's env.
	// AND a RW lock to the FD so that no more than one thread writes it concurrently.
	struct subscription *sub = sub_;
	debug("new thread for subscription @%p", sub);
	int err = 0;
	while (client_needs_patch(sub) && !err) {
		pth_mutex_acquire(&sub->env->wfd, FALSE, NULL);
		err = send_next_patch(sub);
		pth_mutex_release(&sub->env->wfd);
	}
	debug("terminate thread for subscription @%p", sub);
	subscription_del(sub);
	return NULL;
}

