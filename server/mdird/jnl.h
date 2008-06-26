/* Interface to read and write journals and snapshots,
 * and answer a DIFF query.
 * Beware that many threads may access those files concurrently,
 * althoug they are non preemptibles.
 */
#ifndef JNL_H_080623
#define JNL_H_080623

#include <stdbool.h>
#include <unistd.h>	// ssize_t
#include "queue.h"
#include "mdird.h"

struct dir;
struct jnl;

// max size is given in number of versions
int jnl_begin(char const *rootdir, unsigned max_jnl_size);
void jnl_end(void);

// Write from the cursor's position until end of journal onto the given filedesc.
int jnl_copy(struct jnl *, ssize_t, int fd, long long *next_version);

// Add an header into a directory
struct header;
int jnl_add_action(char const *path, char action, struct header *header);

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
	LIST_ENTRY(subscription) dir_entry;	// in the directory list of subscriptions
	struct dir *dir;
	long long version;	// last known version (updated when we send a patch)
	// location of the next one (or NULL if no more since last we checked)
	struct jnl *jnl;
	ssize_t offset;
};

int subscription_new(struct subscription **sub, char const *path, long long version);
void subscription_del(struct subscription *sub);
bool subscription_same_path(struct subscription *sub, char const *path);
int subscription_reset_version(struct subscription *sub, long long version);
static void *subscription_thread(void *sub_);

#endif
