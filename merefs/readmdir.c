#include "scambio/header.h"
#include "merefs.h"

static mdir_version last_read;

/*
 * Initial Read
 */

static void extract_file_info(struct header *h, char const **name, char const **digest, char const **resource)
{
	if_fail (*name     = header_search(h, SC_NAME_FIELD)) return;
	if_fail (*digest   = header_search(h, SC_DIGEST_FIELD)) return;
	if_fail (*resource = header_search(h, SC_RESOURCE_FIELD)) return;
	if (! *name || ! *digest || ! *resource) {
		warning("File header have name '%s', digest '%s' and resource '%s'", *name, *digest, *resource);
		return;
	}
}

static void remove_remote_file(struct header *h)
{
	struct header *target;
	if_fail (target = mdir_get_targeted_header(mdir, h)) return;
	char const *name, *digest, *resource;
	if_fail (extract_file_info(h, &name, &digest, &resource)) return;
	struct file *file = file_search(&unmatched_files, name, resource);
	on_error return;
	if (file) file_del(file);
}

static void add_remote_file(struct mdir *mdir, struct header *header, enum mdir_action action, bool new, union mdir_list_param param, void *data)
{
	(void)mdir;
	(void)data;
	if (action == MDIR_REM) {
		// Event if this is a full reread, we may have pending transiant removals.
		remove_remote_file(header);
		return;
	}
	if (! header_has_type(header, SC_FILE_TYPE)) return;
	char const *name, *digest, *resource;
	if_fail (extract_file_info(header, &name, &digest, &resource)) return;
	debug("Adding file '%s' to unmatched list", name);
	if_fail ((void)file_new(&unmatched_files, name, digest, resource, 0)) return;
	if (! new && param.version > last_read) last_read = param.version;
}

// Will read the whole mdir and create an entry (on unmatched list) for each file
void start_read_mdir(void)
{
	last_read = 0;
	if_fail (mdir_patch_list(mdir, 0, false, add_remote_file, NULL)) return;
	debug("We've now read up to version %"PRIversion, last_read);
}

/*
 * Read new entries
 */

// Will append to unmatched list the new entry
void reread_mdir(void)
{
	debug("Reread from version %"PRIversion, last_read);
	if_fail (mdir_patch_list(mdir, last_read, false, add_remote_file, NULL)) return;
	debug("We've now read up to version %"PRIversion, last_read);
}

// If some remote files are still unmatched, create them
void create_unmatched_files(void)
{
	struct file *file, *tmp;
	LIST_FOREACH_SAFE(file, &unmatched_files, entry, tmp) {
		if_fail (create_local_file(file)) return;
		debug("Promote file '%s' to matched list", file->name);
		LIST_REMOVE(file, entry);
		LIST_INSERT_HEAD(&matched_files, file, entry);
	}
}
