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
#ifndef MDIRB_H_081221
#define MDIRB_H_081221

#include <time.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "merelib.h"

struct sc_msg;
struct mdirb;
struct mdirb_listener {
	LIST_ENTRY(mdirb_listener) entry;
	void (*refresh)(struct mdirb_listener *, struct mdirb *);
};
struct mdirb {
	struct mdir mdir;
	struct mdir_cursor cursor;
	unsigned nb_msgs, nb_unread;
	LIST_HEAD(msgs, sc_msg) msgs;
	/* We keep track of all the dir_views that uses this, so that they all get noticed when
	 * the mdir content changes. msg_views are not noticed because they hold a ref on their
	 * message anyway (if the message is deleted everything in it remains valid).
	 */
	LIST_HEAD(mdirb_listeners, mdirb_listener) listeners;
};

static inline struct mdirb *mdir2mdirb(struct mdir *mdir)
{
	return DOWNCAST(mdir, mdir, mdirb);
}

void mdirb_init(void);

static inline void mdirb_listener_ctor(struct mdirb_listener *listener, struct mdirb *mdirb, void (*cb)(struct mdirb_listener *, struct mdirb *))
{
	listener->refresh = cb;
	LIST_INSERT_HEAD(&mdirb->listeners, listener, entry);
}

static inline void mdirb_listener_dtor(struct mdirb_listener *listener)
{
	LIST_REMOVE(listener, entry);
}

void mdirb_refresh(struct mdirb *);

#endif
