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
#ifndef CALENDAR_H_081222
#include "scambio.h"
#include "scambio/mdir.h"
#include "merelib.h"
#include "vadrouille.h"

/* The custom list for calendar is... a calendar, with a list per day.
 * The other special behaviour is that we try to have only one calendar
 * window ; when we are asked for a new dir_list we merely add the folder
 * to the global calendar window (also, this calendar display a list of
 * folders currently displayed (used to remove a folder from the calendar,
 * or to choose where to add new entries).
 */

void calendar_init(void);

struct cal_date;
struct mdirb;

struct cal_dir_view {
	struct sc_dir_view view;
	GtkWidget *calendar, *event_list;
	GtkListStore *event_store;
	TAILQ_HEAD(day2events, day2event) day2events[31];
	// We may view several dirs in the same dir_view. The only drawback is that
	// we must handle our mdirb_listener ourself.
	unsigned nb_dirs;
	struct cal_dir {
		struct mdirb *mdirb;
		struct mdirb_listener listener;
		struct cal_dir_view *view;	// back link for ease
		char name[PATH_MAX];
	} dirs[32];
};

struct sc_view *cal_editor_view_new(struct cal_dir_view *, struct mdirb *def, struct cal_date *start, struct cal_date *stop, char const *descr, mdir_version replaced);

#endif
