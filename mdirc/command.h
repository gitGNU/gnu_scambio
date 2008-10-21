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
#ifndef COMMAND_H_080923
#define COMMAND_H_080923

#include "scambio/cnx.h"

// commands that were sent for which we wait an answer
struct command {
	LIST_ENTRY(command) mdirc_entry;	// entry in the mdirc list
	struct mdirc *mdirc;	// backlink
	char const *kw;
	char filename[PATH_MAX];	// associated file (for put/rem)
	time_t creation;	// FIXME: should be handled by mdir_cnx
	struct mdir_sent_query sq;
};

// give relative folder (ie mdir name for PUT/REM, id for SUB/UNSUB) and absolute filename
struct command *command_new(char const *kw, struct mdirc *mdirc, char const *folder, char const *filename);
void command_del(struct command *command);
struct command *command_get_by_path(struct mdirc *mdirc, char const *kw, char const *path);
bool command_timeouted(struct command *command);

#endif
