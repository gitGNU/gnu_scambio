#ifndef FILE_H_081112
#define FILE_H_081112

#include "digest.h"

struct file {
	LIST_ENTRY(file) entry;	// on the hash we use to cache local files attributes, or onto unmatched_files for remote files
	char name[PATH_MAX];	// relative to local_path
	char digest[MAX_DIGEST_STRLEN+1];
	char resource[PATH_MAX];
	time_t mtime;	// unset for remote files
};
extern LIST_HEAD(files, file) unmatched_files;	// list of remote files that have no local counterpart
extern struct files matched_files;
#define LOCAL_HASH_SIZE 5000
extern struct files local_hash[LOCAL_HASH_SIZE];

void files_begin(void);
struct file *file_new(struct files *list, char const *name, char const *digest, char const *resource, time_t mtime);
void file_del(struct file *file);
struct file *file_search_by_digest(struct files *list, char const *name, char const *digest);
struct file *file_search_by_mtime(struct files *list, char const *name, time_t mtime);
unsigned file_hash(char const *name);

#endif
