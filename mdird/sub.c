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
#include <assert.h>
#include <stddef.h>
#include "scambio.h"
#include "varbuf.h"
#include "sub.h"
#include "mdird.h"
#include "digest.h"
#include "misc.h"
#include "scambio/header.h"

/*
 * Creation / Deletion / Reset
 */

static void *subscription_thread(void *sub);

static void subscription_ctor(struct subscription *sub, struct cnx_env *env, char const *name, mdir_version version)
{
	sub->version = version;
	sub->env = env;
	struct mdir *mdir = mdir_lookup(name);
	on_error return;
	sub->mdird = mdir2mdird(mdir);
	LIST_INSERT_HEAD(&env->subscriptions, sub, env_entry);
	subscription_reset_version(sub, version);
	sub->thread_id = pth_spawn(PTH_ATTR_DEFAULT, subscription_thread, sub);
	LIST_INSERT_HEAD(&sub->mdird->subscriptions, sub, mdird_entry);
}

struct subscription *subscription_new(struct cnx_env *env, char const *name, mdir_version version)
{
	struct subscription *sub = malloc(sizeof(*sub));
	if (! sub) with_error(ENOMEM, "malloc subscription") return NULL;
	subscription_ctor(sub, env, name, version);
	on_error {
		free(sub);
		return NULL;
	}
	return sub;
}

static void subscription_dtor(struct subscription *sub)
{
	LIST_REMOVE(sub, mdird_entry);
	LIST_REMOVE(sub, env_entry);
	(void)pth_cancel(sub->thread_id);	// better set the cancellation type to PTH_CANCEL_ASYNCHRONOUS
}

void subscription_del(struct subscription *sub)
{
	subscription_dtor(sub);
	free(sub);
}

void subscription_reset_version(struct subscription *sub, mdir_version version)
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
	struct mdir *mdir = mdir_lookup(dir);
	on_error return NULL;
	LIST_FOREACH(sub, &env->subscriptions, env_entry) {
		if (mdir == &sub->mdird->mdir) return sub;
	}
	return NULL;
}

/*
 * Thread
 */

static bool client_needs_patch(struct subscription *sub)
{
	return sub->version < mdir_last_version(&sub->mdird->mdir);
}

static void send_patch(int fd, struct header *h, struct mdir *mdir, enum mdir_action action, mdir_version prev, mdir_version new)
{
	char cmdstr[5+1+PATH_MAX+1+20+1+20+1+1+1];
	size_t cmdlen = snprintf(cmdstr, sizeof(cmdstr), "PATCH %s %"PRIversion" %"PRIversion" %s\n", mdir_id(mdir), prev, new, mdir_action2str(action));
	assert(cmdlen < sizeof(cmdstr));
	Write(fd, cmdstr, cmdlen);
	on_error return;
	header_write(h, fd);
	Write(fd, "\n", 1);
}

static void send_next_patch(struct subscription *sub)
{
	enum mdir_action action;
	mdir_version next_version = sub->version;
	struct header *h = mdir_read_next(&sub->mdird->mdir, &next_version, &action);
	on_error return;
	send_patch(sub->env->fd, h, &sub->mdird->mdir, action, sub->version, next_version);
	unless_error sub->version = next_version;	// last version known is the last we sent
}

static void wait_notif(struct subscription *sub)
{
	(void)sub;
	pth_yield(NULL);
	sleep(1);	// FIXME
}

static void *subscription_thread(void *sub_)
{
	struct subscription *sub = sub_;
	debug("new thread for subscription @%p", sub);
	while (1) {
		if (client_needs_patch(sub)) {
			pth_mutex_acquire(&sub->env->wfd, FALSE, NULL);
			send_next_patch(sub);
			pth_mutex_release(&sub->env->wfd);
			on_error break;
		}
		wait_notif(sub);
	}
	debug("terminate thread for subscription @%p", sub);
	subscription_del(sub);
	return NULL;
}

