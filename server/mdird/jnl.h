/* Interface to read and write journals and snapshots,
 * and answer a DIFF query.
 * Beware that many threads may access those files concurrently,
 * althoug they are non preemptibles.
 */
#ifndef JNL_H_080623
#define JNL_H_080623

#include <stdbool.h>
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

/* 
 * Subscriptions
 * Beware that this structure, being in two very different lists, may be changed
 * by two different threads (the one that handle the client, and another one
 * writing in this director
 */

struct subscription {
	LIST_ENTRY(subscription) env_entry;	// in the client list of subscriptions. THE ONLY FIELD OTHER MODULES CAN USE
	LIST_ENTRY(subscription) dir_entry;	// in the directory list of subscriptions
	struct dir *dir;
	long long version;	// last known version
	// location of the next one (or NULL if no more since last we checked)
	struct jnl *jnl;
	ssize_t offset;
};

bool subscription_same_path(struct subscription *sub, char const *path);
int subscription_reset_version(struct subscription *sub, long long version);
int subscription_new(struct subscription **sub, char const *path, long long version);
void subscription_del(struct subscription *sub);

#endif
