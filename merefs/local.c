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
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "scambio/channel.h"
#include "scambio/header.h"
#include "misc.h"
#include "merefs.h"

static void make_file_patch(struct header *header, struct file *file)
{
	if_fail ((void)header_field_new(header, SC_TYPE_FIELD, SC_FILE_TYPE)) return;
	if_fail ((void)header_field_new(header, SC_NAME_FIELD, file->name)) return;
	if_fail ((void)header_field_new(header, SC_DIGEST_FIELD, file->digest)) return;
	if_fail ((void)header_field_new(header, SC_RESOURCE_FIELD, file->resource)) return;
}

static void wait_complete(void)
{
	debug("waiting...");
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
	while (! chn_cnx_all_tx_done(&ccnx)) pth_nanosleep(&ts, NULL);
}

static void upload_file(struct file *file, char const *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) with_error(errno, "open(%s)", filename) return;
	// Send file contents for this resource,
	// or reuse an existing resource if the content is already up there
	// (for instance if we renamed the file)
	struct file *remote = file_search_by_digest(&unmatched_files, file->digest);
	if (! remote) remote = file_search_by_digest(&matched_files, file->digest);
	if (! remote) remote = file_search_by_digest(&removed_files, file->digest);
	if (remote) {
		debug("Content is known remotely as content for file '%s'", remote->name);
		snprintf(file->resource, sizeof(file->resource), "%s", remote->resource);
	} else {
		debug("New content, upload it");
		if_fail (chn_send_file_request(&ccnx, filename, file->resource)) return;
		wait_complete();
	}
	// Now patch the mdir : send a patch to advertize the new file
	struct header *header;
	if_fail (header = header_new()) return;
	make_file_patch(header, file);
	unless_error mdir_patch_request(mdir, MDIR_ADD, header);
	header_unref(header);
}

static void remove_remote(struct file *file)
{
	if_fail (mdir_del_request(mdir, file->version)) return;
}

static void traverse_dir_rec(char *dirpath, int dirlen);
static void scan_opened_dir(DIR *dir, char *dirpath, int dirlen)
{
	struct dirent *dirent;
	while (NULL != (dirent = readdir(dir))) {
		debug("Dir entry '%s' of type %d", dirent->d_name, dirent->d_type);
		if (dirent->d_name[0] == '.' || dirent->d_type == DT_LNK) {
			// Skip ".", ".." and any hidden file (like our persistent TS).
			// Skip also synlinks to avoid loops.
			debug("...skip");
			continue;
		}
		if (dirent->d_type == DT_DIR) {	// recurse
			debug("...recurse");
			int new_dirlen = dirlen + snprintf(dirpath+dirlen, sizeof(dirpath)-dirlen, "/%s", dirent->d_name);
			traverse_dir_rec(dirpath, new_dirlen);
			dirpath[dirlen] = '\0';
			on_error return;
		} else if (dirent->d_type == DT_REG) {
			debug("...plain file");
			snprintf(dirpath+dirlen, sizeof(dirpath)-dirlen, "/%s", dirent->d_name);
			struct stat statbuf;
			if (0 != stat(dirpath, &statbuf)) {
				error_push(errno, "stat(%s)", dirpath);
quit:
				dirpath[dirlen] = '\0';
				return;
			}
			// We want only one file under a given name (no versionning to this point).
			// Remote files can only be matched by name. Once matched, we must compare
			// the digests. But computing the digest is too expensive so we use the
			// local_hash cache.
			// Look for this filename and mtime in our local files hash
			char const *name = dirpath + local_path_len + 1;
			unsigned h = file_hash(name);
			struct file *file = file_search(local_hash+h, name);
			if (file) {	// The file is there, but did we change it ?
				debug("file already there");
				if (file->mtime != statbuf.st_mtime) {	// yes
					debug("...but was changed");
					file_del(file, local_hash+h);	// will be updated to reflect reality
					file = NULL;
				}
			}
			if (! file) {
				debug("Compute its digest and insert it into the cache for the future");
				char digest[MAX_DIGEST_STRLEN+1];
				if_fail (digest_file(digest, dirpath)) goto quit;
				if_fail (file = file_new(local_hash+h, name, digest, "", statbuf.st_mtime, 0)) goto quit;
			}
			// Now that we have the digest, look for a match for this file in the unmatched_files
			struct file *remote = file_search(&unmatched_files, name);
			if (remote) {	// found
				debug("Found a remote file");
				STAILQ_REMOVE(&unmatched_files, remote, file, entry);
				STAILQ_INSERT_HEAD(&matched_files, remote, entry);
				if (0 == strcmp(file->digest, remote->digest)) {
					debug("Same files !");
				} else {
					debug("Digest mismatch : local file was changed");
					// Add the new local file and then remove the former one
					// FIXME: in between, there are two files with the same name. Easy to deal with when reading the mdir
					if_fail (upload_file(file, dirpath)) goto quit;
					if_fail (remove_remote(remote)) goto quit;
				}
			} else {
				debug("No remote counterpart");
				// So its not on the remote server. Maybe because it's a new file, maybe because it's deleted
				// To find out, compare file's mtime to our last saved mtime.
				if (statbuf.st_mtime < last_run_start()) {
					debug("...because it was deleted (age = %ds)", (int)(last_run_start()-statbuf.st_mtime));
					if (0 != unlink(dirpath)) with_error(errno, "unlink(%s)", dirpath) goto quit;
				} else {
					debug("...because we just added it (age = %ds)", (int)(last_run_start()-statbuf.st_mtime));
					if_fail (upload_file(file, dirpath)) goto quit;
				}
			}
			dirpath[dirlen] = '\0';
		} else {
			debug("...ignoring");
		}
	}
}

static void traverse_dir_rec(char *dirpath, int dirlen)
{
	DIR *dir = opendir(dirpath);
	if (! dir) with_error(errno, "opendir(%s)", dirpath) return;
	scan_opened_dir(dir, dirpath, dirlen);
	closedir(dir);
}

// Will match each local file against its mdir entry
void traverse_local_path(void)
{
	char dirpath[PATH_MAX];
	int len = snprintf(dirpath, sizeof(dirpath), "%s", local_path);
	if_fail (traverse_dir_rec(dirpath, len)) return;
}

void create_local_file(struct file *file)
{
	debug("new local file '%s', resource '%s'", file->name, file->resource);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/%s", local_path, file->name);
	if_fail (Mkdir_for_file(path)) return;
	char cache_file[PATH_MAX];
	if_fail (chn_get_file(&ccnx, cache_file, file->resource)) return;
	// If possible, make a hard link from local to the cache
	if (0 != unlink(path)) {
		if (errno != ENOENT) with_error(errno, "unlink(%s)", path) return;
	}
	if (0 == link(cache_file, path)) {
		debug("hardlinked from the cache");
		return;	// done !
	}
	if (errno != EMLINK && errno != EPERM && errno != EXDEV) {	// these errors are permited
		with_error(errno, "link(%s <- %s)", cache_file, path) return;
	}
	// Copy from the cache file to the local path, then
	int fd = creat(path, 0644);
	if (fd < 0) with_error(errno, "creat(%s)", path) return;
	do {
		// Copy the content from cache to local_path
		int cache_fd = open(cache_file, O_RDONLY);
		if (cache_fd < 0) with_error(errno, "open(%s)", cache_file) break;
		Copy(fd, cache_fd);
		(void)close(cache_fd);
	} while (0);
	(void)close(fd);
}
