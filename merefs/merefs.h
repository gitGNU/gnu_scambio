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
#ifndef MEREFS_H_081112
#define MEREFS_H_081112

#include <stdbool.h>
#include <time.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/channel.h"
#include "file.h"

extern char const *mdir_name;
extern char const *local_path;
extern unsigned local_path_len;
extern struct mdir *mdir;
extern bool quit;
extern struct chn_cnx ccnx;

time_t last_run_start(void);
void start_read_mdir(void);
void reread_mdir(void);
void unmatch_all(void);
void traverse_local_path(void);
void create_unmatched_files(void);
void create_local_file(struct file *file);

#endif
