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
#ifndef MERECAL_H_081008
#define MERECAL_H_081008

#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "merelib.h"

/*
 * Event structure
 */

struct cal_date {
	guint year;	// 0 means the whole structure is undefined (ending dates are optional)
	guint month;	// from 0 to 11
	guint day;	// from 1 to 31
	guint hour;	// from 0 to 23, >23 if hour and min are not defined (hour+min are optional)
	guint min;	// from 0 to 59
	char str[4+1+2+1+2+1+5+1];
};

static inline bool cal_date_is_set(struct cal_date const *cd) { return cd->year != 0; }
static inline bool cal_date_has_time(struct cal_date const *cd) { return cal_date_is_set(cd) && cd->hour <= 23; }
static inline void cal_date_dtor(struct cal_date *cd) { (void)cd; }
void cal_date_ctor(struct cal_date *, guint y, guint M, guint d, guint h, guint m);
void cal_date_ctor_from_input(struct cal_date *, char const *input);
int cal_date_compare(struct cal_date const *a, struct cal_date const *b);
void cal_date_to_str(struct cal_date *, char *, size_t);

extern LIST_HEAD(cal_folders, cal_folder) cal_folders;

struct cal_event;
struct cal_folder {
	LIST_ENTRY(cal_folder) entry;
	char path[PATH_MAX];
	char *name;	// points onto path
	struct mdir *mdir;
	mdir_version last_version;
	LIST_HEAD(cal_events, cal_event) new_events;	// we keep an eye on those that are not synched yet
	bool displayed;
};

extern struct cal_events cal_events;

struct cal_event {
	LIST_ENTRY(cal_event) entry;	// cal_events are ordered according to (day, hour)
	LIST_ENTRY(cal_event) new_entry;	// if version=0, we are on the new_event of our folder
	struct cal_folder *folder;
	struct cal_date start, stop;
	char *description;
	mdir_version version;	// 0 if no synched yet
};

void foreach_event_between(struct cal_date *start, struct cal_date *stop, void (*cb)(struct cal_event *));

/*
 * GUI functions
 */

void refresh(void);
GtkWidget *make_cal_window(void);
GtkWidget *make_edit_window(struct cal_folder *, struct cal_date *start, struct cal_date *end, char const *descr, mdir_version replaced);

#endif
