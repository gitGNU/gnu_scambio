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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "scambio/timetools.h"
#include "merecal.h"

/*
 * Dates
 */

extern inline bool cal_date_has_time(struct cal_date const *cd);
extern inline bool cal_date_is_set(struct cal_date const *cd);
extern inline void cal_date_dtor(struct cal_date *cd);

void cal_date_ctor(struct cal_date *cd, guint y, guint M, guint d, guint h, guint m)
{
	cd->year = y;
	cd->month = M;
	cd->day = d;
	cd->hour = h;
	cd->min = m;
	if (! cal_date_is_set(cd)) {
		cd->str[0] = '\0';
	} else {
		int len = snprintf(cd->str, sizeof(cd->str), "%04u-%02u-%02u", cd->year, cd->month+1, cd->day);
		if (cal_date_has_time(cd)) {
			snprintf(cd->str+len, sizeof(cd->str)-len, " %02uh%02u", cd->hour, cd->min);
		}
	}
}

static void cal_date_ctor_from_str(struct cal_date *cd, char const *str)
{
	unsigned year, month, day, hour, min, sec;
	bool hour_set;
	if_fail (sc_gmfield2uint(str, &year, &month, &day, &hour, &min, &sec, &hour_set)) return;
	month -= 1;
	if (! hour_set) {
		hour = 99;
		min = 99;
	}
	cal_date_ctor(cd, year, month, day, hour, min);
}

void cal_date_to_str(struct cal_date *cd, char *str, size_t size)
{
	assert(cal_date_is_set(cd));
	int len;
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	tm.tm_year = cd->year - 1900;
	tm.tm_mon  = cd->month;
	tm.tm_mday = cd->day;
	tm.tm_isdst = -1;
	if (cal_date_has_time(cd)) {
		tm.tm_hour = cd->hour;
		tm.tm_min  = cd->min;
	}
	len = snprintf(str, size, "%s", sc_tm2gmfield(&tm, cal_date_has_time(cd)));
	if (len >= (int)size) error_push(0, "Buffer too small");
}


static bool is_leap_year(unsigned y)
{
	return (y%4 == 0) && (y%100 != 0 || y%400 == 0);
}

int month_days(unsigned year, unsigned month)	// month is from 0 to 11
{
	static unsigned const days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return days[month] + (month == 1 && is_leap_year(year));
}

static bool issep(int c)
{
	return isblank(c) || c == '-' || c == '/' || c == ':' || c == 'h';
}

void cal_date_ctor_from_input(struct cal_date *cd, char const *i)
{
	long year = 0, month = 0, day = 0, hour = 99, min = 0;
	// Try to reckognize a date and a time from user inputs
	while (isblank(*i)) i++;
	if (*i != '\0') {
		char const *j;
		year = strtol(i, (char **)&j, 10);
		if (*j == '\0') with_error(0, "Invalid date : lacks a month") return;
		while (issep(*j)) j++;
		month = strtol(j, (char **)&i, 10);
		if (*i == '\0') with_error(0, "Invalid date : lacks a day") return;
		if (month < 1 || month > 12) with_error(0, "Invalid month (%ld)", month) return;
		month --;
		while (issep(*i)) i++;
		day = strtol(i, (char **)&j, 10);
		if (day < 1 || day > 31) with_error(0, "Invalid day (%ld)", day) return;
		if (day > month_days(year, month)) with_error(0, "No such day (%ld) in this month (%ld)", day, month) return;
		while (isblank(*j)) j++;
		if (*j != '\0') {
			hour = strtol(j, (char **)&i, 10);
			if (hour < 0 || hour > 23) with_error(0, "Invalid hour (%ld)", hour) return;
			while (issep(*i)) i++;
			if (*i != '\0') {
				min = strtol(i, (char **)&j, 10);
				if (min < 0 || min > 59) with_error(0, "Invalid minutes (%ld)", min) return;
			}
		}
	}
	cal_date_ctor(cd, year, month, day, hour, min);
}

static int guint_compare(guint a, guint b)
{
	if (a < b) return -1;
	if (a > b) return 1;
	return 0;
}

int cal_date_compare(struct cal_date const *a, struct cal_date const *b)
{
	debug("compare %s and %s", a->str, b->str);
#	define COMPARE_FIELD(f) if (a->f != b->f) return guint_compare(a->f, b->f)
	COMPARE_FIELD(year);
	COMPARE_FIELD(month);
	COMPARE_FIELD(day);
	if (! cal_date_has_time(a)) {
		if (cal_date_has_time(b) && (b->hour > 0 || b->min > 0)) return -1;
		return 0;
	}
	if (! cal_date_has_time(b) && (a->hour > 0 || a->min > 0)) return 1;
	COMPARE_FIELD(hour);
	COMPARE_FIELD(min);
	return 0;
#	undef COMPARE_FIELD
}

/*
 * Events
 */

struct cal_events cal_events = LIST_HEAD_INITIALIZER(&cal_events);

// insert in the event list, ordered by start date
static void cal_event_insert(struct cal_event *ce)
{
	struct cal_event *e;
	LIST_FOREACH(e, &cal_events, entry) {
		if (cal_date_compare(&e->start, &ce->start) > 0) {
			debug("insert '%s' before '%s'", ce->description, e->description);
			LIST_INSERT_BEFORE(e, ce, entry);
			return;
		} else if (! LIST_NEXT(e, entry)) {
			debug("insert '%s' after last one '%s'", ce->description, e->description);
			LIST_INSERT_AFTER(e, ce, entry);
			return;
		}
	}
	debug("insert '%s' as sole", ce->description);
	LIST_INSERT_HEAD(&cal_events, ce, entry);
}

static void cal_event_ctor(struct cal_event *ce, struct cal_folder *cf, struct cal_date const *start, struct cal_date const *stop, char const *descr, mdir_version version)
{
	ce->folder = cf;
	ce->start = *start;
	ce->stop = *stop;
	debug("event from %s to %s", start->str, stop->str);
	if (cal_date_is_set(stop) && cal_date_compare(start, stop) > 0) with_error(0, "event stop > start") return;
	ce->version = version;
	ce->description = descr ? strdup(descr) : NULL;
	cal_event_insert(ce);
	on_error return;
	if (0 == ce->version) {	// not synchronized yet
		LIST_INSERT_HEAD(&cf->new_events, ce, new_entry);
	} else if (cf->last_version < ce->version) {
		cf->last_version = ce->version;
	}
}

static struct cal_event *cal_event_new(struct cal_folder *cf, struct cal_date const *start, struct cal_date const *stop, char const *descr, mdir_version version)
{
	struct cal_event *ce = malloc(sizeof(*ce));
	if (! ce) with_error(ENOMEM, "malloc cal_event") return NULL;
	cal_event_ctor(ce, cf, start, stop, descr, version);
	on_error {
		free(ce);
		ce = NULL;
	}
	return ce;
}

static void cal_event_dtor(struct cal_event *ce)
{
	debug("%s", ce->description);
	LIST_REMOVE(ce, entry);
	if (ce->version == 0) LIST_REMOVE(ce, new_entry);
	cal_date_dtor(&ce->start);
	cal_date_dtor(&ce->stop);
	if (ce->description) free(ce->description);
}

static void cal_event_del(struct cal_event *ce)
{
	cal_event_dtor(ce);
	free(ce);
}

void foreach_event_between(struct cal_date *start, struct cal_date *stop, void (*cb)(struct cal_event *, void *), void *data)
{
	struct cal_event *ce;
	LIST_FOREACH(ce, &cal_events, entry) {
		debug("test event '%s' in between %s and %s", ce->description, start->str, stop->str);
		if (cal_date_compare(&ce->start, stop) > 0) {
			debug("start after end");
			break;	// cal_events are sorted by start date
		}
		if (! ce->folder->displayed) {
			debug(" skip because not displayed");
			continue;
		}
		struct cal_date *end = cal_date_is_set(&ce->stop) ? &ce->stop : &ce->start;
		if (cal_date_compare(end, start) < 0) {
			debug("  skip because end date (%s) before start (%s)", end->str, start->str);
			continue;
		}
		cb(ce, data);
	}
}

/*
 * Folders
 */

struct cal_folders cal_folders = LIST_HEAD_INITIALIZER(&cal_folders);

static void cal_folder_ctor(struct cal_folder *cf, char const *path)
{
	debug("New cal_folder @%p for %s", cf, path);
	int len = snprintf(cf->path, sizeof(cf->path), "%s", path);
	if (len >= (int)sizeof(cf->path)) with_error(0, "Path too long : %s", path) return;
	cf->name = cf->path + len;
	cf->last_version = 0;
	while (cf->name > cf->path && *(cf->name-1) != '/') cf->name--;
	if_fail (cf->mdir = mdir_lookup(path)) return;
	cf->displayed = true;	// TODO: save user prefs somewhere
	LIST_INIT(&cf->new_events);
	LIST_INSERT_HEAD(&cal_folders, cf, entry);
}

static struct cal_folder *cal_folder_new(char const *path)
{
	struct cal_folder *cf = malloc(sizeof(*cf));
	if (! cf) with_error(ENOMEM, "malloc cal_folder") return NULL;
	cal_folder_ctor(cf, path);
	on_error {
		free(cf);
		cf = NULL;
	}
	return cf;
}

#if 0
static void cal_folder_del_events(struct cal_folder *cf)
{
	struct cal_event *ce, *tmp;
	LIST_FOREACH_SAFE(ce, &cal_events, entry, tmp) {
		if (ce->folder == cf) cal_event_del(ce);
	}
}

static void cal_folder_dtor(struct cal_folder *cf)
{
	cal_folder_del_events(cf);
	LIST_REMOVE(cf, entry);
}

static void cal_folder_del(struct cal_folder *cf)
{
	cal_folder_dtor(cf);
	free(cf);
}
#endif

/*
 * Refresh
 * (reload calendar data)
 */

static void add_event_cb(struct mdir *mdir, struct header *header, enum mdir_action action, bool new, union mdir_list_param param, void *data)
{
	debug("action=%s, new=%c", action==MDIR_ADD ? "add":"rem", new ? 'y':'n');
	(void)mdir;
	if (action == MDIR_REM) {
		mdir_version target = header_target(header);
		on_error return;
		struct cal_event *ce, *tmp;
		LIST_FOREACH_SAFE(ce, &cal_events, entry, tmp) {
			if (ce->folder->mdir == mdir && ce->version && ce->version == target) {
				cal_event_del(ce);
				break;
			}
		}
		return;
	}
	assert(action == MDIR_ADD);
	struct cal_folder *cf = (struct cal_folder *)data;
	mdir_version version = new ? 0 : param.version;
	struct cal_date start, stop;
	char const *start_str = header_search(header, SC_START_FIELD);
	if (start_str) {
		if_fail (cal_date_ctor_from_str(&start, start_str)) return;
	} else {
		error("Invalid calendar message with no "SC_START_FIELD" field");
		return;
	}
	debug("new event is version %lld, start str = '%s'", version, start_str);
	char const *stop_str = header_search(header, SC_STOP_FIELD);
	if (stop_str) {
		if_fail (cal_date_ctor_from_str(&stop, stop_str)) {
			error_save();
			cal_date_dtor(&start);
			error_restore();
			return;
		}
	} else {
		cal_date_ctor(&stop, 0, 0, 0, 0, 0);
	}
	char const *desc = header_search(header, SC_DESCR_FIELD);
	unless_error (void)cal_event_new(cf, &start, &stop, desc, version);
	on_error error_clear();	// forget this event, go ahead with others
	cal_date_dtor(&stop);
	cal_date_dtor(&start);
}

static void refresh_folder(struct cal_folder *cf)
{
	debug("refreshing folder %s", cf->name);
	// First we delete all the events that were not versionned yet
	struct cal_event *ce;
	while (NULL != (ce = LIST_FIRST(&cf->new_events))) {
		assert(ce->version == 0);
		cal_event_del(ce);
	}
	// Then we ask for the newer or not synched events
	mdir_patch_list(cf->mdir, cf->last_version+1, false, add_event_cb, cf);
}

void refresh(void)
{
	debug("refresh");
	struct cal_folder *cf;
	LIST_FOREACH(cf, &cal_folders, entry) {
		refresh_folder(cf);
		on_error return;
	}
}

/*
 * Main
 */

int main(int nb_args, char *args[])
{
	if_fail(init("merecal.log", nb_args, args)) return EXIT_FAILURE;
	char const *folders[] = { "/calendars/rixed", "/calendars/project-X" };
	for (unsigned f=0; f<sizeof_array(folders); f++) {
		(void)cal_folder_new(folders[f]);
		on_error return EXIT_FAILURE;
	}
	refresh();
	GtkWidget *cal_window = make_cal_window();
	if (! cal_window) return EXIT_FAILURE;
	gtk_widget_show_all(cal_window);
	gtk_main();
	return EXIT_SUCCESS;
}
