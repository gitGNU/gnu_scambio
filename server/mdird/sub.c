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
		// TODO: take writer lock on env->fd (which we need)
		err = send_next_patch(sub);
		// TODO: release env->fd lock
	}
	debug("terminate thread for subscription @%p", sub);
	subscription_del(sub);
	return NULL;
}

