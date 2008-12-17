/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <string.h>
#include "scambio/header.h"
#include "merefs.h"

/*
 * Initial Read
 */

static void extract_file_info(struct header *h, char const **name, char const **digest, char const **resource)
{
	*name = *digest = *resource = NULL;
	struct header_field *hf;
	if (NULL != (hf = header_find(h, SC_NAME_FIELD, NULL))) *name = hf->value;
	if (NULL != (hf = header_find(h, SC_DIGEST_FIELD, NULL))) *digest = hf->value;
	if (NULL != (hf = header_find(h, SC_RESOURCE_FIELD, NULL))) *resource = hf->value;
	if (! *name || ! *digest || ! *resource) {
		warning("File header have name '%s', digest '%s' and resource '%s'", *name, *digest, *resource);
		return;
	}
}

static void remove_remote_file(mdir_version to_del)
{
	struct file *file = file_search_by_version(&unmatched_files, to_del);
	on_error return;
	if (file) {
		file_del(file, &unmatched_files);
	};
}

static void rem_remote_file(struct mdir *mdir, mdir_version version, void *data)
{
	(void)mdir;
	(void)data;
	// Event if this is a full reread, we may have pending transiant removals.
	remove_remote_file(version);
}

static void add_remote_file(struct mdir *mdir, struct header *header, mdir_version version, void *data)
{
	(void)mdir;
	(void)data;
	if (! header_has_type(header, SC_FILE_TYPE)) return;
	char const *name, *digest, *resource;
	if_fail (extract_file_info(header, &name, &digest, &resource)) return;
	debug("Adding file '%s' to unmatched list", name);
	/* We allow only one file per name.
	 * We may have severall patch for the same name if :
	 * - the patch is not synched yet (thus has no version)
	 * - someone f*cked up
	 * To deal with this, we look for former unmatched_files with same name,
	 * and delete them (thus keeping the last one (ie bigger version, or a new one)
	 */
	struct file *file = file_search(&unmatched_files, name);  
	if (file) {
		debug("...we already had a file for that name (resource was '%s') : delete it", file->resource);
		file_del(file, &unmatched_files);
		file = NULL;
	} else {
		debug("...this name is new");
	}
	if_fail ((void)file_new(&unmatched_files, name, digest, resource, 0, version)) return;
}

/*
 * Read new entries
 */

void read_mdir(void)
{
	if_fail (mdir_patch_list(mdir, false, add_remote_file, rem_remote_file, NULL)) return;
}

// If some remote files are still unmatched, create them
void create_unmatched_files(void)
{
	debug("Create all files still not matched");
	struct file *file, *tmp;
	STAILQ_FOREACH_SAFE(file, &unmatched_files, entry, tmp) {
		if_fail (create_local_file(file)) return;
		debug("Promote file '%s' to matched list", file->name);
		STAILQ_REMOVE(&unmatched_files, file, file, entry);
		STAILQ_INSERT_HEAD(&matched_files, file, entry);
	}
}

void unmatch_all(void)
{
	debug("Unmatching all matched files");
	STAILQ_CONCAT(&unmatched_files, &matched_files);
}

