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
#ifndef MDIRC_H_080731
#define MDIRC_H_080731

#include <stddef.h>
#include <limits.h>
#include <stdbool.h>
#include <pth.h>
#include "scambio/mdir.h"
#include "command.h"
#include "scambio/cmd.h"
#include "scambio/cnx.h"

typedef void *thread_entry(void *);
thread_entry connecter_thread, reader_thread, writer_thread;
pth_t reader_pthid, writer_pthid;
extern struct mdir_cnx cnx;

void reader_begin(void);
void reader_end(void);
void writer_begin(void);
void writer_end(void);
void connecter_begin(void);
void connecter_end(void);
struct command;
struct c2l_map;
void finalize_sub  (struct mdir_cmd *cmd, void *user_data);
void finalize_unsub(struct mdir_cmd *cmd, void *user_data);
void finalize_put  (struct mdir_cmd *cmd, void *user_data);
void finalize_rem  (struct mdir_cmd *cmd, void *user_data);
void finalize_quit (struct mdir_cmd *cmd, void *user_data);
void finalize_auth (struct mdir_cmd *cmd, void *user_data);
void patch_service (struct mdir_cmd *cmd, void *user_data);

struct mdirc {
	struct mdir mdir;
 	// The writer will add entries to these lists while the reader will remove them once completed,
	// and perform the proper final action for these commands.
	LIST_HEAD(commands, command) commands;
	LIST_HEAD(c2l_maps, c2l_map) c2l_maps;
	bool subscribed;
	pth_rwlock_t command_lock;	// protects all the previous commands + command lists
	LIST_HEAD(patches, patch) patches;
	unsigned nb_pending_acks;
};

static inline struct mdirc *mdir2mdirc(struct mdir *mdir)
{
	return (struct mdirc *)((char *)mdir - offsetof(struct mdirc, mdir));
}

void push_path(char const *path);

#endif
