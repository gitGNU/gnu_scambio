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
#include <string.h>
#include <assert.h>
#include "scambio.h"
#include "scambio/timetools.h"
#include "scambio/header.h"
#include "scambio/mdirc.h"
#include "scambio/msg.h"
#include "misc.h"

/*
 * Ctor/Dtor
 */

static void sc_msg_default_del(struct sc_msg *msg)
{
	sc_msg_dtor(msg);
	free(msg);
}

// Throws no error.
void sc_msg_ctor(struct sc_msg *msg, struct mdirc *mdirc, struct header *h, mdir_version version, int status, struct sc_msg *marked)
{
	debug("msg@%p, version %"PRIversion, msg, version);
	static struct sc_msg_ops const my_ops = {
		.del = sc_msg_default_del,
	};
	msg->ops = &my_ops;
	msg->mdirc = mdirc;
	msg->header = header_ref(h);
	msg->version = version;
	msg->status = status;
	LIST_INIT(&msg->marks);
	msg->marked = marked;
	if (marked) {
		LIST_INSERT_HEAD(&marked->marks, msg, marks_entry);	// no ordering of marks ?
	}
	msg->count = 1;
}

static struct sc_msg *default_msg_new(struct mdirc *mdirc, struct header *h, mdir_version version)
{
	struct sc_msg *msg = Malloc(sizeof(*msg));
	sc_msg_ctor(msg, mdirc, h, version);
	return msg;
}

void sc_msg_dtor(struct sc_msg *msg)
{
	debug("msg@%p", msg);
	assert(msg->count <= 0);
	header_unref(msg->header);
	msg->header = NULL;
	//  Cascade the del to all the marks
	struct sc_msg *mark;
	while (NULL != (mark = LIST_FIRST(&msg->marks))) {
		LIST_REMOVE(mark, marks_entry);
		mark->marked = NULL;
		sc_msg_unref(mark);
	}
	// And if I'm a mark myself, remove me from my parent marks
	if (msg->marked) {
		LIST_REMOVE(msg, marks_entry);
		msg->marked = NULL;
	}
}

struct sc_msg *(*sc_msg_new)(struct mdirc *, struct header *, mdir_version) = default_msg_new;

extern inline struct sc_msg *sc_msg_ref(struct sc_msg *msg);
extern inline void sc_msg_unref(struct sc_msg *msg);

void sc_msg_mark(struct sc_msg *msg, char const *field, char const *value)
{
	// FIXME: check that this mark does not already exist
	debug("Mark msg %"PRIversion" with %s: %s", msg->version, field, value);
	struct header *h = header_new();
	(void)header_field_new(h, SC_TYPE_FIELD, SC_MARK_TYPE);
	(void)header_field_new(h, SC_TARGET_FIELD, mdir_version2str(msg->version));
	(void)header_field_new(h, field, value);
	mdir_patch_request(&msg->mdirc->mdir, MDIR_ADD, h);
	header_unref(h);
	mdirc_update(msg->mdirc);	// which will add it to our list of marks
}

/*
 * Iterator on all msg headers
 */

extern inline void sc_msg_cursor_ctor(struct sc_msg_cursor *, struct sc_msg *);
extern inline void sc_msg_cursor_dtor(struct sc_msg_cursor *);

void sc_msg_cursor_next(struct sc_msg_cursor *cursor, char const *name)
{
	struct sc_msg *msg, *next_msg;
	if (cursor->mark) {
		msg = cursor->mark;
		next_msg = LIST_NEXT(msg, marks_entry);
	} else {
		msg = cursor->msg;
		next_msg = LIST_FIRST(&msg->marks);
	}
	cursor->hf = msg->error_status == 0 ? header_find(msg->header, name, cursor->hf) : NULL;
	if (cursor->hf) return;
	// not found in this msg, try next.
	if (cursor->mark) sc_msg_unref(cursor->mark);
	cursor->mark = next_msg;
	if (! next_msg) return;	// no more
	sc_msg_ref(cursor->mark);
	sc_msg_cursor_next(cursor, name);
}

