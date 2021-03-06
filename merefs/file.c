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
#include <stdlib.h>
#include <string.h>
#include "merefs.h"

/*
 * File entry : a structure to cache info about local files or to keep what's on the mdir
 */

struct files unmatched_files, matched_files, removed_files;

static char const *sanitize(char const *name)
{
	// We store the name without the leading slash
	while (*name == '/') name++;
	return name;
}

static void file_ctor(struct file *file, struct files *list, char const *name, char const *digest, char const *resource, mdir_version version)
{
	snprintf(file->name, sizeof(file->name), "%s", sanitize(name));
	snprintf(file->digest, sizeof(file->digest), "%s", digest);
	snprintf(file->resource, sizeof(file->resource), "%s", resource ? resource : "");
	file->version = version;
	STAILQ_INSERT_HEAD(list, file, entry);
}

struct file *file_new(struct files *list, char const *name, char const *digest, char const *resource, mdir_version version)
{
	struct file *file = malloc(sizeof(*file));
	if (! file) with_error(ENOMEM, "malloc(file)") return NULL;
	if_fail (file_ctor(file, list, name, digest, resource, version)) {
		free(file);
		file = NULL;
	}
	return file;
}

static void file_dtor(struct file *file, struct files *list)
{
	STAILQ_REMOVE(list, file, file, entry);
}

void file_del(struct file *file, struct files *list)
{
	file_dtor(file, list);
	free(file);
}

void free_file_list(struct files *list)
{
	struct file *file;
	while (NULL != (file = STAILQ_FIRST(list))) {
		file_del(file, list);
	}
}

struct file *file_search(struct files *list, char const *name)
{
	name = sanitize(name);

	struct file *file;
	STAILQ_FOREACH(file, list, entry) {
		if (0 == strcmp(name, file->name)) return file;
	}
	return NULL;
}

struct file *file_search_by_version(struct files *list, mdir_version version)
{
	struct file *file;
	STAILQ_FOREACH(file, list, entry) {
		if (file->version == version) return file;
	}
	return NULL;
}

struct file *file_search_by_digest(struct files *list, char const *digest)
{
	struct file *file;
	STAILQ_FOREACH(file, list, entry) {
		if (0 == strcmp(file->digest, digest)) return file;
	}
	return NULL;
}

void file_set_digest(struct file *file, char const *digest)
{
	snprintf(file->digest, sizeof(file->digest), "%s", digest);
}

/*
 * Init
 */

static void files_end(void)
{
	free_file_list(&unmatched_files);
	free_file_list(&matched_files);
	free_file_list(&removed_files);
}

void files_begin(void)
{
	STAILQ_INIT(&unmatched_files);
	STAILQ_INIT(&matched_files);
	STAILQ_INIT(&removed_files);
	atexit(files_end);
}

