/* Interface to read and write journals and snapshots,
 * and answer a DIFF query.
 * Beware that many threads may access those files concurrently,
 * althoug they are non preemptibles.
 */
#ifndef JNL_H_080623
#define JNL_H_080623

#include <stdbool.h>
#include "queue.h"

struct dir;
struct jnl {
	STAILQ_ENTRY(jnl) entry;
	int fd;	// -1 if not open
	long long first_version, last_version;
	struct dir *dir;
};

// max size is given in number of versions
int jnl_begin(char const *rootdir, unsigned max_jnl_size);
void jnl_end(void);

// Gives a ref to a journal containing the given version, and returns its offset
// returns -ENOMSG if there is no such version yet
ssize_t jnl_find(struct jnl **jnl, char const *path, long long version);
void jnl_release(struct jnl *);

// Write from the cursor's position until end of journal onto the given filedesc.
int jnl_copy(struct jnl *, off_t, int fd);

// Add an header into a directory
struct header;
int jnl_add_action(char const *path, char action, struct header *header);

#endif
