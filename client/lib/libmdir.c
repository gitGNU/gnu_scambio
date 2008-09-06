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
#include <stdio.h>
#include <errno.h>
#include "scambio.h"
#include "mdir.h"

size_t mdir_root_len;
char mdir_root[PATH_MAX];

int mdir_begin(void)
{
	int err;
	debug("init libmdir");
	if (0 != (err = conf_set_default_str("MDIR_ROOT_DIR", "/tmp/mdir/"))) return err;
	mdir_root_len = snprintf(mdir_root, sizeof(mdir_root), "%s", conf_get_str("MDIR_ROOT_DIR"));
	if (mdir_root_len >= sizeof(mdir_root)) {
		error("MDIR_ROOT_DIR too long");
		return -EINVAL;
	}
	while (mdir_root_len > 0 && mdir_root[mdir_root_len-1] == '/') mdir_root[--mdir_root_len] = '\0';
	if (mdir_root_len == 0) {
		error("MDIR_ROOT_DIR must not be empty");
		return -EINVAL;
	}
	return 0;
}

void mdir_end(void)
{
	debug("end libmdir");
}

bool mdir_folder_exists(char const *folder)
{
	(void)folder;
	// TODO
	return true;
}


