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
#ifndef MDIRC_H_081221
#define MDIRC_H_081221

#include <time.h>
#include "scambio.h"
#include "scambio/mdir.h"

struct sc_msg;
struct mdirc {
	struct mdir mdir;
	struct mdir_cursor cursor;
	unsigned nb_msgs;
	TAILQ_HEAD(msgs, sc_msg) msgs;	// only toplevel msgs are here (not marks)
};

static inline struct mdirc *mdir2mdirc(struct mdir *mdir)
{
	return DOWNCAST(mdir, mdir, mdirc);
}

void mdirc_ctor(struct mdirc *);
void mdirc_dtor(struct mdirc *);

struct mdir *mdirc_default_alloc(void);
void mdirc_default_free(struct mdir *);

void mdirc_update(struct mdirc *);

#endif
