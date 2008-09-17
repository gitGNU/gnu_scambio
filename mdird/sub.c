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
 * Listener ops
 */

static void listener_del(struct mdir_listener *l, struct mdir *mdir)
{
	(void)mdir;
	struct subscription *sub = (struct subscription *)((char *)l - offsetof(struct subscription, listener));
	subscription_del(sub);
}

static void listener_notify(struct mdir_listener *l, struct mdir *mdir, struct header *h)
{
	(void)mdir;
	(void)h;
	struct subscription *sub = (struct subscription *)((char *)l - offsetof(struct subscription, listener));
	pth_message_t *m = malloc(sizeof(*m));
	if (! m) with_error(ENOMEM, "malloc pth_message") return;
	m->m_size = 0;	// in case someone want to have a look
	m->m_data = NULL;
	pth_msgport_put(sub->msg_port, m);
}

/*
 * Creation / Deletion / Reset
 */

static void *subscription_thread(void *sub);

static void subscription_ctor(struct subscription *sub, struct cnx_env *env, char const *name, mdir_version version)
{
	sub->version = version;
	sub->env = env;
	sub->mdir = mdir_lookup(name);
	on_error return;
	LIST_INSERT_HEAD(&env->subscriptions, sub, env_entry);
	subscription_reset_version(sub, version);
	sub->msg_port = pth_msgport_create(NULL);
	sub->thread_id = pth_spawn(PTH_ATTR_DEFAULT, subscription_thread, sub);
	static struct mdir_listener_ops const my_ops = {
		.del = listener_del,
		.notify = listener_notify,
	};
	mdir_listener_ctor(&sub->listener, &my_ops);
	sub->registered = false;
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

static void clear_msgport(struct subscription *sub)
{
	pth_message_t *m;
	while (NULL != (m = pth_msgport_get(sub->msg_port))) {
		free(m);
	}
}

static void subscription_dtor(struct subscription *sub)
{
	if (sub->registered) mdir_unregister_listener(sub->mdir, &sub->listener);
	mdir_listener_dtor(&sub->listener);
	LIST_REMOVE(sub, env_entry);
	// waiting messages must be fetched and freed
	clear_msgport(sub);
	pth_msgport_destroy(sub->msg_port);
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
		if (mdir == sub->mdir) return sub;
	}
	return NULL;
}

/*
 * Thread
 */

static bool client_needs_patch(struct subscription *sub)
{
	return sub->version < mdir_last_version(sub->mdir);
}

static void send_patch(int fd, struct header *h, struct mdir *mdir, enum mdir_action action, mdir_version prev, mdir_version new)
{
	char cmdstr[5+1+PATH_MAX+1+20+1+20+1+1+1];
	size_t cmdlen = snprintf(cmdstr, sizeof(cmdstr), "PATCH %s %"PRIversion" %"PRIversion" %c\n", mdir_id(mdir), prev, new, action == MDIR_ADD ? '+':'-');
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
	struct header *h = mdir_read_next(sub->mdir, &next_version, &action);
	on_error return;
	send_patch(sub->env->fd, h, sub->mdir, action, sub->version, next_version);
	unless_error sub->version = next_version;	// last version known is the last we sent
}

static void wait_notif(struct subscription *sub)
{
	pth_event_t ev = pth_event(PTH_EVENT_MSG, sub->msg_port);
	(void)pth_wait(ev);
	pth_event_free(ev, PTH_FREE_ALL);
	clear_msgport(sub);
}

static void *subscription_thread(void *sub_)
{
	struct subscription *sub = sub_;
	debug("new thread for subscription @%p", sub);
	while (1) {
		if (! client_needs_patch(sub)) {
			if (! sub->registered) {
				mdir_register_listener(sub->mdir, &sub->listener);
				sub->registered = true;
			}
			wait_notif(sub);
		}
		pth_mutex_acquire(&sub->env->wfd, FALSE, NULL);
		send_next_patch(sub);
		pth_mutex_release(&sub->env->wfd);
		on_error break;
	}
	debug("terminate thread for subscription @%p", sub);
	subscription_del(sub);
	return NULL;
}

