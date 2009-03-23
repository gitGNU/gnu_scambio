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
/*
 * A high level view of a message.
 */
#ifndef MSG_H_081222
#define MSG_H_081222

#include "scambio/mdir.h"

struct mdirc;
struct sc_msg;

struct sc_msg_ops {
	void (*del)(struct sc_msg *);
};

struct sc_msg {
	struct sc_msg_ops const *ops;
	TAILQ_ENTRY(sc_msg) mdirc_entry;
	LIST_ENTRY(sc_msg) marks_entry;
	struct mdirc *mdirc;	// backlink
	struct header *header;	// original header
	mdir_version version;
	int status;	// 0 if the message is OK
	LIST_HEAD(sc_marks, sc_msg) marks;	// list of marks referencing this message
	struct sc_msg *marked;
	int count;
};

// You may want to change the default builder :
extern struct sc_msg *(*sc_msg_new)(struct mdirc *, struct header *, mdir_version, int, struct sc_msg *);

void sc_msg_ctor(struct sc_msg *, struct mdirc *, struct header *, mdir_version, int, struct sc_msg *);
void sc_msg_dtor(struct sc_msg *);

static inline struct sc_msg *sc_msg_ref(struct sc_msg *msg)
{
	msg->count++;
	return msg;
}

static inline void sc_msg_unref(struct sc_msg *msg)
{
	if (--msg->count <= 0) msg->ops->del(msg);
}

// Add a mark to this msg (and request the patch for it)
void sc_msg_mark(struct sc_msg *, char const *field, char const *value);

// Iterate through all the headers of this msg and its (valid) marks
struct sc_msg_cursor {
	struct sc_msg *msg, *mark;
	struct header_field *hf;
};

static inline void sc_msg_cursor_ctor(struct sc_msg_cursor *cursor, struct sc_msg *msg)
{
	cursor->msg = sc_msg_ref(msg);
	cursor->mark = NULL;
	cursor->hf = NULL;
}

static inline void sc_msg_cursor_dtor(struct sc_msg_cursor *cursor)
{
	sc_msg_unref(cursor->msg);
	if (cursor->mark) {
		sc_msg_unref(cursor->mark);
	}
}

// Look for given name (or all if name is NULL) in (valid) msg headers
void sc_msg_cursor_next(struct sc_msg_cursor *, char const *name);

#endif
