/* Interface to read and write journals and snapshots,
 * and answer a DIFF query.
 * Beware that many threads may access those files concurrently,
 * althoug they are non preemptibles.
 */
#ifndef JNL_H_080623
#define JNL_H_080623

#include <stdbool.h>
#include <unistd.h>	// ssize_t
#include <pth.h>
#include "queue.h"
#include "mdird.h"

struct dir;
struct jnl;
struct stribution;

// max size is given in number of versions
int jnl_begin(void);
void jnl_end(void);

// Add an header into a directory
struct header;
int jnl_add_patch(char const *path, char action, struct header *header);
// version is the version we want to patch
int jnl_send_patch(long long *actual_version, struct dir *dir, long long version, int fd);

int dir_get(struct dir **dir, char const *path);
int strib_get(struct stribution **, char const *path);

bool dir_same_path(struct dir *dir, char const *path);
long long dir_last_version(struct dir *dir);
void dir_register_subscription(struct dir *dir, struct subscription *sub);

#endif
