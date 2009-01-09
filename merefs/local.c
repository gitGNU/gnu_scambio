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
#include "map.h"

static void wait_complete(void)
{
	debug("waiting...");
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
	while (! chn_cnx_all_tx_done(&ccnx)) pth_nanosleep(&ts, NULL);
}

static void keep_map(char const *fname, char const *digest)
{
	map_set(&next_map, fname, digest);
}

static void remote_is_master(struct file *file, char const *fpath)
{
	debug("Creating or Updating local file '%s', resource '%s'", file->name, file->resource);
	if (! background) printf("New remote file '%s'...\n", file->name);
	
	if_fail (Mkdir_for_file(fpath)) return;

	char cache_file[PATH_MAX];
	if_fail (chn_get_file(&ccnx, cache_file, file->resource)) return;

	// If possible, make a hard link from local to the cache
	if (0 != unlink(fpath)) {
		if (errno != ENOENT) with_error(errno, "unlink(%s)", fpath) return;
	}
	if (0 == link(cache_file, fpath)) {
		debug("hardlinked from cache entry '%s'", cache_file);
	} else if (errno == EMLINK || errno == EPERM || errno == EXDEV) {	// these errors are permited
		// Copy from the cache file to the local fpath, then
		int fd = creat(fpath, 0644);
		if (fd < 0) with_error(errno, "creat(%s)", fpath) return;
		do {
			// Copy the content from cache to local_path
			int cache_fd = open(cache_file, O_RDONLY);
			if (cache_fd < 0) with_error(errno, "open(%s)", cache_file) break;
			Copy(fd, cache_fd);
			(void)close(cache_fd);
		} while (0);
		(void)close(fd);
	} else {
		with_error(errno, "link(%s <- %s)", cache_file, fpath) return;
	}

	// Now update the file map
	map_set(&next_map, file->name, file->digest);
}

static void local_is_master(struct file *remote_file, char const *fpath, char const *fname, char const *digest)
{
	char resource[PATH_MAX];
	
	if (! background) printf("New local file : %s\n", fname);
	// Send file contents for this resource,
	// or reuse an existing resource if the content is already up there
	// (for instance if we renamed the file)
	struct file *remote = file_search_by_digest(&unmatched_files, digest);
	if (! remote) remote = file_search_by_digest(&matched_files, digest);
	if (! remote) remote = file_search_by_digest(&removed_files, digest);
	if (remote) {
		debug("Content is already known remotely as content for file '%s'", remote->name);
		snprintf(resource, sizeof(resource), "%s", remote->resource);
	} else {
		debug("New content, upload it");
		if (! background) printf("   ...uploading\n");
		if_fail (chn_send_file_request(&ccnx, fpath, resource)) return;
		wait_complete();
	}

	// Now patch the mdir : send a patch to advertize the new file
	struct header *header;
	if_fail (header = header_new()) return;
	(void)header_field_new(header, SC_TYPE_FIELD, SC_FILE_TYPE);
	(void)header_field_new(header, SC_NAME_FIELD, fname);
	(void)header_field_new(header, SC_DIGEST_FIELD, digest);
	(void)header_field_new(header, SC_RESOURCE_FIELD, resource);
	mdir_patch_request(mdir, MDIR_ADD, header);
	header_unref(header);
	on_error return;

	if (remote_file) {	// Remove old version from the mdir
		if_fail (mdir_del_request(mdir, remote_file->version)) return;
	}
	
	// Now update the file map
	map_set(&next_map, fname, digest);
}

static void conflict(char const *fpath)
{
	// Build backup file name (remember fname is a PATH).
	char bak[PATH_MAX];
	int len = snprintf(bak, sizeof(bak), "%s", fpath);
	if (len == PATH_MAX) {
		error("Cannot build a backup file for '%s' : name would be too long", fpath);
		return;	// so bad
	}
	char *c;
	for (c = bak+len+1; c > bak+local_path_len && *(c-1) != '/'; c--) {
		*c = *(c-1);
	}
	*c = '.';

	warning("Saving conflictuous file '%s' into '%s'", fpath, bak);
	(void)unlink(bak);
	if (0 != rename(fpath, bak)) with_error(errno, "rename(%s -> %s)", fpath, bak) return;
}

static void remove_local_file(char const *fpath)
{
	if (! background) printf("Removing local file '%s'\n", fpath);
	if (0 != unlink(fpath)) with_error(errno, "unlink(%s)", fpath) return;
}

static void match_local_file(char const *fpath, char const *fname, char const *digest)
{
	debug("Working on local file '%s' with digest '%s'", fname, digest);
	// Look for corresponding R and M files
	struct file *remote_file = file_search(&unmatched_files, fname);
	char const *map_digest = map_get_digest(&current_map, fname);

	if (remote_file) {	// Remove from the unmatched list
		debug("Found a remote file with digest '%s'", remote_file->digest);
		STAILQ_REMOVE(&unmatched_files, remote_file, file, entry);
		STAILQ_INSERT_HEAD(&matched_files, remote_file, entry);
	}

	if (remote_file && map_digest) {
		if (0 == strcmp(digest, map_digest)) {
			debug("...in synch with map record");
			if (0 == strcmp(digest, remote_file->digest)) {
				debug("...and the remote file");
				keep_map(fname, map_digest);
			} else {
				debug("...but remote file was changed");
				remote_is_master(remote_file, fpath);
			}
		} else {
			debug("...local file was changed");
			if (0 == strcmp(map_digest, remote_file->digest)) {
				debug("...but not remote file");
				local_is_master(remote_file, fpath, fname, digest);
			} else {
				debug("...and so do remote file !");
				conflict(fpath);
				remote_is_master(remote_file, fpath);
			}
		}
	} else if (remote_file && !map_digest) {
		debug("...which just appeared under the same name than a remote file !");
		if (0 == strcmp(digest, remote_file->digest)) {
			debug("...and they are the same ! (map file was lost ?)");
			keep_map(fname, digest);
		} else {
			conflict(fpath);
			remote_is_master(remote_file, fpath);
		}
	} else if (map_digest && !remote_file) {
		if (0 == strcmp(digest, map_digest)) {
			debug("...which was deleted on server");
			remove_local_file(fpath);
		} else {
			debug("...which was deleted on server but changed locally !");
			conflict(fpath);
			remove_local_file(fpath);
		}
	} else {	// not in map nor remote
		debug("...wich is a new file");
		local_is_master(NULL, fpath, fname, digest);
	}
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
			char digest[MAX_DIGEST_STRLEN+1];
			if_succeed (digest_file(digest, dirpath)) {
				match_local_file(dirpath, dirpath+local_path_len, digest);
			}
			dirpath[dirlen] = '\0';
			on_error return;
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
	while (len > 0 && dirpath[len-1] == '/') len--;
	if_fail (traverse_dir_rec(dirpath, len)) return;
}

// If some remote files are still unmatched, create them
void create_unmatched_files(void)
{
	debug("Create all files still not matched");
	char fpath[PATH_MAX];
	struct file *file, *tmp;
	STAILQ_FOREACH_SAFE(file, &unmatched_files, entry, tmp) {
		snprintf(fpath, sizeof(fpath), "%s/%s", local_path, file->name);
		char const *map_digest = map_get_digest(&current_map, file->name);
		
		if (! map_digest) {
			debug("Remote file '%s' is a new file", file->name);
			if_fail (remote_is_master(file, fpath)) return;
			STAILQ_REMOVE(&unmatched_files, file, file, entry);
			STAILQ_INSERT_HEAD(&matched_files, file, entry);
		} else {
			debug("Local file '%s' was deliberately removed", file->name);
			if_fail (mdir_del_request(mdir, file->version)) return;
		}
	}
}


