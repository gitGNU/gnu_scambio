#ifndef FILE_H_081112
#define FILE_H_081112

#include "digest.h"
#include "scambio/mdir.h"

struct file {
	STAILQ_ENTRY(file) entry;	// on the hash we use to cache local files attributes, or onto unmatched_files for remote files
	char name[PATH_MAX];	// relative to local_path
	char digest[MAX_DIGEST_STRLEN+1];
	char resource[PATH_MAX];
	mdir_version version;	// unset for local files
	time_t mtime;	// unset for remote files
};
extern STAILQ_HEAD(files, file) unmatched_files;	// list of remote files that have no local counterpart
extern struct files matched_files;
#define LOCAL_HASH_SIZE 5000
extern struct files local_hash[LOCAL_HASH_SIZE];

void files_begin(void);
struct file *file_new(struct files *list, char const *name, char const *digest, char const *resource, time_t mtime, mdir_version version);
void file_del(struct file *file, struct files *list);
struct file *file_search(struct files *list, char const *name);
struct file *file_search_by_version(struct files *list, mdir_version version);
unsigned file_hash(char const *name);

#endif
