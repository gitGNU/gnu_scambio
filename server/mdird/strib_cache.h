#ifndef CONF_CACHE_H_080603
#define CONF_CACHE_H_080603

#include <time.h>
#include <queue.h>
#include <limits.h>
#include "stribution.h"

struct strib_cached {
	struct stribution *stribution;	// variable size so pointed, but 1 to 1 relation
	LIST_ENTRY(strib_cached) entry;
	time_t last_used;
	char path[PATH_MAX];
};

int strib_cache_begin(char const *root_path);
void strib_cache_end(void);
struct stribution *strib_cache_get(char const *path);

#endif
