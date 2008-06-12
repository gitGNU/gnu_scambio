#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "conf_cache.h"

/*
 * Data Definitions
 */

LIST_HEAD(conf_cache, conf_cached) conf_cache;
char const *root_path;

/*
 * Private Functions
 */

static void conf_cached_dtor(struct conf_cached *cached)
{
	if (cached->conf) {
		conf_del(cached->conf);
		cached->conf = NULL;
	}
	LIST_REMOVE(cached, entry);
}

static void conf_cached_del(struct conf_cached *cached)
{
	conf_cached_dtor(cached);
	free(cached);
}

static int conf_cached_ctor(struct conf_cached *cached, char const *path)
{
	char fullfile[PATH_MAX];
	(void)snprintf(fullfile, sizeof(fullfile), "%s/%s/.aiguil.conf", root_path, path);
	cached->conf = conf_new(fullfile);
	if (! cached->conf) return -1;
	snprintf(cached->path, sizeof(cached->path), "%s", path);
	time(&cached->last_used);
	LIST_INSERT_HEAD(&conf_cache, cached, entry);
	return 0;
}

static struct conf_cached *conf_cached_new(char const *path)
{
	struct conf_cached *cached = malloc(sizeof(*cached));
	if (! cached) return NULL;
	if (0 != conf_cached_ctor(cached, path)) {
		free(cached);
		return NULL;
	}
	return cached;
}

/*
 * Public Functions
 */

int conf_cache_begin(char const *root_path_)
{
	root_path = root_path_;
	// TODO check that root_path is a directory
	LIST_INIT(&conf_cache);
	return 0;
}

void conf_cache_end(void)
{
	struct conf_cached *cached;
	while ((cached = LIST_FIRST(&conf_cache))) {
		conf_cached_del(cached);
	}
}

struct conf *conf_cache_get(char const *path)
{
	struct conf_cached *cached;
	LIST_FOREACH(cached, &conf_cache, entry) {
		if (0 == strcmp(path, cached->path)) {
			time(&cached->last_used);
			return cached->conf;
		}
	}
	cached = conf_cached_new(path);
	if (! cached) return NULL;
	return cached->conf;
}

