#ifndef SUB_H_080627
#define SUB_H_080627

#include <unistd.h>	// ssize_t
#include <pth.h>
#include "queue.h"

struct dir;
struct jnl;
struct cnx_env;

/* Beware that this structure, being in two very different lists, may be changed
 * by THREE different threads :
 * - the one that handle the client requests,
 * - the one that handle a client subscription,
 * - and any other one writing in this directory.
 * Use directory mutex for safety !
 */

struct subscription {
	// Managed by client thread
	LIST_ENTRY(subscription) env_entry;	// in the client list of subscriptions.
	struct cnx_env *env;
	pth_t thread_id;
	// Managed by JNL module from here
	LIST_ENTRY(subscription) dir_entry;	// in the directory list of subscriptions
	struct dir *dir;
	long long version;	// last known version (updated when we send a patch)
};

int subscription_new(struct subscription **sub, struct cnx_env *env, char const *path, long long version);
void subscription_del(struct subscription *sub);
struct subscription *subscription_find(struct cnx_env *env, char const *path);
void subscription_reset_version(struct subscription *sub, long long version);

#endif
