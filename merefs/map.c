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
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "scambio.h"
#include "misc.h"
#include "merefs.h"
#include "file.h"
#include "map.h"

void map_load(struct files *files)
{
	char map_fname[PATH_MAX];
	snprintf(map_fname, sizeof(map_fname), "%s/.map", local_path);
	debug("Loading sync map from '%s'", map_fname);
	
	int fd = open(map_fname, O_RDONLY|O_CREAT, 0640);
	if (fd < 0) with_error(errno, "open(%s)", map_fname) return;

	STAILQ_INIT(files);
	do {
		char path[PATH_MAX], digest[MAX_DIGEST_STRLEN+1];
		if_fail (Read(path, fd, sizeof(path))) {
			if (error_code() == 0) {	// EOF
				error_clear();
			}
			break;
		}
		if_fail (Read(digest, fd, sizeof(digest))) break;
		(void)file_new(files, path, digest, NULL, 0);
	} while (! is_error());

	(void)close(fd);
}

void map_save(struct files *files)
{
	char map_fname[PATH_MAX];
	snprintf(map_fname, sizeof(map_fname), "%s/.map", local_path);
	debug("Saving sync map to '%s'", map_fname);

	int fd = open(map_fname, O_WRONLY, 0640);
	if (fd < 0) with_error(errno, "open(%s)", map_fname) return;

	struct file *file;
	STAILQ_FOREACH(file, files, entry) {
		if_fail (Write(fd, file->name, sizeof(file->name))) break;
		if_fail (Write(fd, file->digest, sizeof(file->digest))) break;
	}

	(void)close(fd);
}

char const *map_get_digest(struct files *files, char const *fname)
{
	struct file *file = file_search(files, fname);
	return file ? file->digest : NULL;
}

void map_set(struct files *files, char const *fname, char const *digest)
{
	struct file *file = file_search(files, fname);
	if (file) {
		file_set_digest(file, digest);
	} else {
		(void)file_new(files, fname, digest, NULL, 0);
	}
}

void map_del(struct files *files, char const *fname)
{
	struct file *file = file_search(files, fname);
	if (! file) return;
	file_del(file, files);
}

