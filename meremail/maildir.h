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
#ifndef MSGMDIR_H_081128
#define MSGMDIR_H_081128

#include <time.h>
#include "scambio.h"
#include "scambio/mdir.h"

struct maildir;
struct msg {
	LIST_ENTRY(msg) entry;
	struct maildir *maildir;	// backlink for ease
	char *from;
	char *descr;
	time_t date;
	mdir_version version;
};

struct maildir {
//	LIST_ENTRY(maildir) entry;	// in maildirs
	struct mdir mdir;
	struct mdir_cursor cursor;
	LIST_HEAD(msgs, msg) msgs;
	unsigned nb_msgs;
};

//LIST_HEAD(maildirs, maildir) maildirs;

static inline struct maildir *mdir2maildir(struct mdir *mdir)
{
	return DOWNCAST(mdir, mdir, maildir);
}

void maildir_init(void);
void maildir_refresh(struct maildir *maildir);
char const *ts2staticstr(time_t ts);

#endif
