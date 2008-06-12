#ifndef CONF_CACHE_H_080603
#define CONF_CACHE_H_080603

#include <time.h>
#include <queue.h>
#include <limits.h>
#include "conf.h"

struct conf_cached {
	struct conf *conf;	// variable size so pointed, but 1 to 1 relation
	LIST_ENTRY(conf_cached) entry;
	time_t last_used;
	char path[PATH_MAX];
};

int conf_cache_begin(char const *root_path);
void conf_cache_end(void);
struct conf *conf_cache_get(char const *path);

#endif
