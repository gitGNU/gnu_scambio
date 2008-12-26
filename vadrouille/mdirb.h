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
struct mdirb {
	struct mdir mdir;
	struct mdir_cursor cursor;
	unsigned nb_msgs;
	LIST_HEAD(msgs, sc_msg) msgs;
	char name[PATH_MAX];
};

static inline struct mdirb *mdir2mdirb(struct mdir *mdir)
{
	return DOWNCAST(mdir, mdir, mdirb);
}

static inline unsigned mdirb_size(struct mdirb *mdirb)
{
	return mdirb->nb_msgs;
}

void mdirb_init(void);
void mdirb_refresh(struct mdirb *);
static inline char const *mdirb_name(struct mdirb *mdirb)
{
	return mdirb->name;
}
void mdirb_set_name(struct mdirb *, char const *);

#endif
