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
	LIST_HEAD(msgs, sc_msg) msgs;
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

// To be notified whenever a message is created anywhere

struct sc_msg_listener {
	LIST_ENTRY(sc_msg_listener) entry;
	void (*cb)(struct sc_msg_listener *, struct mdirc *, enum mdir_action, struct sc_msg *);
};

void sc_msg_listener_ctor(struct sc_msg_listener *, void (*cb)(struct sc_msg_listener *, struct mdirc *, enum mdir_action, struct sc_msg *));
void sc_msg_listener_dtor(struct sc_msg_listener *);

#endif
