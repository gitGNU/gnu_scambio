#include <stdlib.h>
#include <string.h>
#include "merefs.h"

/*
 * File entry : a structure to cache info about local files or to keep what's on the mdir
 */

struct files unmatched_files, matched_files;
struct files local_hash[LOCAL_HASH_SIZE];

static void file_ctor(struct file *file, struct files *list, char const *name, char const *digest, char const *resource, time_t mtime)
{
	snprintf(file->name, sizeof(file->name), "%s", name);
	snprintf(file->digest, sizeof(file->digest), "%s", digest);
	snprintf(file->resource, sizeof(file->resource), "%s", resource);
	file->mtime = mtime;
	LIST_INSERT_HEAD(list, file, entry);
}

struct file *file_new(struct files *list, char const *name, char const *digest, char const *resource, time_t mtime)
{
	struct file *file = malloc(sizeof(*file));
	if (! file) with_error(ENOMEM, "malloc(file)") return NULL;
	if_fail (file_ctor(file, list, name, digest, resource, mtime)) {
		free(file);
		file = NULL;
	}
	return file;
}

static void file_dtor(struct file *file)
{
	LIST_REMOVE(file, entry);
}

void file_del(struct file *file)
{
	file_dtor(file);
	free(file);
}

static void free_file_list(struct files *list)
{
	struct file *file;
	while (NULL != (file = LIST_FIRST(list))) {
		file_del(file);
	}
}

struct file *file_search_by_digest(struct files *list, char const *name, char const *digest)
{
	struct file *file;
	LIST_FOREACH(file, list, entry) {
		if (0 == strcmp(name, file->name) && 0 == strcmp(digest, file->digest)) return file;
	}
	return NULL;
}

struct file *file_search_by_mtime(struct files *list, char const *name, time_t mtime)
{
	struct file *file;
	LIST_FOREACH(file, list, entry) {
		if (file->mtime == mtime && 0 == strcmp(name, file->name)) return file;
	}
	return NULL;
}

static unsigned hashstr(char const *str)
{
	// dumb hash func
	unsigned h = 0;
	for (char *c = str; *c; c++) {
		h += *c;
	}
	return h;
}

unsigned file_hash(char const *name)
{
	unsigned h = hashstr(name);
	return h % LOCAL_HASH_SIZE;
}

/*
 * Init
 */

static void files_end(void)
{
	free_file_list(&unmatched_files);
	free_file_list(&matched_files);
	for (unsigned h=0; h<sizeof(local_hash); h++) {
		free_file_list(local_hash+h);
	}
}

void files_begin(void)
{
	LIST_INIT(&unmatched_files);
	LIST_INIT(&matched_files);
	for (unsigned h=0; h<sizeof(local_hash); h++) {
		LIST_INIT(local_hash+h);
	}
	atexit(files_end);
}


