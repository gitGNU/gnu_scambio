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
#ifndef MSG_H_081222
#define MSG_H_081222

#include "vadrouille.h"

struct mdirb;
struct sc_plugin;

struct sc_msg {
	LIST_ENTRY(sc_msg) entry;
	struct header *header;
	mdir_version version;
	struct mdirb *mdirb;	// backlink for easy manipulation of mdirb->msg_count
	struct sc_plugin *plugin;	// plugin responsible for this, or NULL if none
	bool was_read;
	int count;
};

void sc_msg_ctor(struct sc_msg *, struct mdirb *, struct header *, mdir_version, struct sc_plugin *);
void sc_msg_dtor(struct sc_msg *);

static inline struct sc_msg *sc_msg_ref(struct sc_msg *msg)
{
	msg->count++;
	return msg;
}

static inline void sc_msg_unref(struct sc_msg *msg)
{
	if (--msg->count <= 0) msg->plugin->ops->msg_del(msg);
}

// To be notified whenever a message is created
struct sc_msg_listener {
	LIST_ENTRY(sc_msg_listener) entry;
	void (*cb)(struct sc_msg_listener *, struct mdirb *, struct sc_msg *);
};

void sc_msg_listener_ctor(struct sc_msg_listener *, void (*cb)(struct sc_msg_listener *, struct mdirb *, struct sc_msg *));
void sc_msg_listener_dtor(struct sc_msg_listener *);

void sc_msg_init(void);

#endif
