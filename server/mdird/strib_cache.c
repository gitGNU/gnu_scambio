#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "strib_cache.h"

/*
 * Data Definitions
 */

LIST_HEAD(strib_cache, strib_cached) strib_cache;
char const *root_path;

/*
 * Private Functions
 */

static void strib_cached_dtor(struct strib_cached *cached)
{
	if (cached->stribution) {
		strib_del(cached->stribution);
		cached->stribution = NULL;
	}
	LIST_REMOVE(cached, entry);
}

static void strib_cached_del(struct strib_cached *cached)
{
	strib_cached_dtor(cached);
	free(cached);
}

static int strib_cached_ctor(struct strib_cached *cached, char const *path)
{
	char fullfile[PATH_MAX];
	(void)snprintf(fullfile, sizeof(fullfile), "%s/%s/.stribution", root_path, path);
	cached->stribution = strib_new(fullfile);
	if (! cached->stribution) return -1;
	snprintf(cached->path, sizeof(cached->path), "%s", path);
	time(&cached->last_used);
	LIST_INSERT_HEAD(&strib_cache, cached, entry);
	return 0;
}

static struct strib_cached *strib_cached_new(char const *path)
{
	struct strib_cached *cached = malloc(sizeof(*cached));
	if (! cached) return NULL;
	if (0 != strib_cached_ctor(cached, path)) {
		free(cached);
		return NULL;
	}
	return cached;
}

/*
 * Public Functions
 */

int strib_cache_begin(char const *root_path_)
{
	root_path = root_path_;
	// TODO check that root_path is a directory
	LIST_INIT(&strib_cache);
	return 0;
}

void strib_cache_end(void)
{
	struct strib_cached *cached;
	while ((cached = LIST_FIRST(&strib_cache))) {
		strib_cached_del(cached);
	}
}

struct stribution *strib_cache_get(char const *path)
{
	struct strib_cached *cached;
	LIST_FOREACH(cached, &strib_cache, entry) {
		if (0 == strcmp(path, cached->path)) {
			time(&cached->last_used);
			return cached->stribution;
		}
	}
	cached = strib_cached_new(path);
	if (! cached) return NULL;
	return cached->stribution;
}

