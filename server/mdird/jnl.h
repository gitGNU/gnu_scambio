/* Interface to read and write journals and snapshots,
 * and answer a DIFF query.
 * Beware that many threads may access those files concurrently,
 * althoug they are non preemptibles.
 */
#ifndef JNL_H_080623
#define JNL_H_080623

#include <stdbool.h>
#include "queue.h"

struct jnl {
	STAILQ_ENTRY(jnl) entry;
	int fd;	// -1 if not open
	long long first_version, last_version;
};

int jnl_begin(char const *rootdir);
void jnl_end(void);

// Returns a ref to a journal containing the given version, and set its offset
struct jnl *jnl_find(off_t *offset, char const *path, long long version);
void jnl_release(struct jnl *);

// Write from the cursor's position until end of journal onto the given filedesc.
int jnl_copy(struct jnl *, off_t offset, int fd);

#endif
