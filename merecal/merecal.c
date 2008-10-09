#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "merecal.h"

/*
 * Dates
 */

// Yngvar Folling's function appeared in comp.lang.c.moderated on 2000/04/16
static time_t mkgmtime(struct tm *utc_tm)
{
   time_t timeval;
   struct tm timestruct;

   timestruct = *utc_tm;
   timestruct.tm_isdst = 0;
   timeval = mktime(&timestruct);
   timestruct = *gmtime(&timeval);

   /* timestruct now contains time in the "negative" of your time zone. */

   timestruct.tm_sec   = utc_tm->tm_sec  * 2 - timestruct.tm_sec;
   timestruct.tm_min   = utc_tm->tm_min  * 2 - timestruct.tm_min;
   timestruct.tm_hour  = utc_tm->tm_hour * 2 - timestruct.tm_hour;
   timestruct.tm_mday  = utc_tm->tm_mday * 2 - timestruct.tm_mday;
   timestruct.tm_mon   = utc_tm->tm_mon  * 2 - timestruct.tm_mon;
   timestruct.tm_year  = utc_tm->tm_year * 2 - timestruct.tm_year;
   timestruct.tm_isdst = 0;

   /* timestruct now contains local time, but without normalized fields. */

   timeval = mktime(&timestruct);
   *utc_tm = *gmtime(&timeval);
   return timeval;
}

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
	unsigned year, month, day, hour, min;
	int ret = sscanf(str, "%u %u %u %u %u", &year, &month, &day, &hour, &min);
	month -= 1;
	if (ret == 3) {	// OK, make it a date only (no timezone adjustement)
		hour = 99;
		min = 99;
	} else if (ret == 5) {	// Adjust timezone (we were given UTC timestamp
		struct tm tm;
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = year - 1900;
		tm.tm_mon  = month;
		tm.tm_mday = day;
		tm.tm_hour = hour;
		tm.tm_min  = min;
		time_t t = mkgmtime(&tm);
		struct tm *now = localtime(&t);
		year  = now->tm_year + 1900;
		month = now->tm_mon;
		day   = now->tm_mday;
		hour  = now->tm_hour;
		min   = now->tm_min;
		debug("UTC '%s' converted to local '%u %u %u %u %u'", str, year, month+1, day, hour, min);
	} else with_error(0, "Cannot convert string '%s' to date", str) return;
	cal_date_ctor(cd, year, month, day, hour, min);
}

void cal_date_to_str(struct cal_date *cd, char *str, size_t size)
{
	assert(cal_date_is_set(cd));
	int len;
	if (! cal_date_has_time(cd)) {
		len = snprintf(str, size, "%04u %02u %02u",
			(unsigned)cd->year, (unsigned)cd->month+1, (unsigned)cd->day);
	} else {
		struct tm tm;
		memset(&tm, 0, sizeof(tm));
		tm.tm_year = cd->year - 1900;
		tm.tm_mon  = cd->month;
		tm.tm_mday = cd->day;
		tm.tm_hour = cd->hour;
		tm.tm_min  = cd->min;
		tm.tm_isdst = -1;
		time_t t = mktime(&tm);	// now we have the timestamp
		tm = *gmtime(&t);	// now we have the GM timestruct
		len = snprintf(str, size, "%04u %02u %02u %02u %02u",
			(unsigned)tm.tm_year+1900, (unsigned)tm.tm_mon+1, (unsigned)tm.tm_mday, (unsigned)tm.tm_hour, (unsigned)tm.tm_min);
	}
	if (len >= (int)size) error_push(0, "Buffer too small");
}


static bool is_leap_year(unsigned y)
{
	return (y%4 == 0) && (y%100 != 0 || y%400 == 0);
}

static int month_days(unsigned year, unsigned month)	// month is from 0 to 11
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
//	debug("compare %s and %s", a->str, b->str);
#	define COMPARE_FIELD(f) if (a->f != b->f) return guint_compare(a->f, b->f)
	COMPARE_FIELD(year);
	COMPARE_FIELD(month);
	COMPARE_FIELD(day);
	if (! cal_date_has_time(a) || ! cal_date_has_time(b)) return 0;
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
			debug("insert before this one");
			LIST_INSERT_BEFORE(e, ce, entry);
			return;
		} else if (! LIST_NEXT(e, entry)) {
			debug("insert after this last one");
			LIST_INSERT_AFTER(e, ce, entry);
			return;
		}
	}
	debug("insert as first");
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
	LIST_REMOVE(ce, entry);
	cal_date_dtor(&ce->start);
	cal_date_dtor(&ce->stop);
	if (ce->description) free(ce->description);
}

static void cal_event_del(struct cal_event *ce)
{
	cal_event_dtor(ce);
	free(ce);
}

void foreach_event_between(struct cal_date *start, struct cal_date *stop, void (*cb)(struct cal_event *))
{
	struct cal_event *ce;
	LIST_FOREACH(ce, &cal_events, entry) {
		debug("test event '%s' in between %s and %s", ce->description, start->str, stop->str);
		if (cal_date_compare(&ce->start, stop) > 0) break;	// cal_events are sorted by start date
		if (! ce->folder->displayed) {
			debug(" skip because not displayed");
			continue;
		}
		struct cal_date *end = cal_date_is_set(&ce->stop) ? &ce->stop : &ce->start;
		if (cal_date_compare(end, start) < 0) {
			debug("  skip because end date (%s) before start (%s)", end->str, start->str);
			continue;
		}
		cb(ce);
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
	while (cf->name > cf->path && *(cf->name-1) != '/') cf->name--;
	if_fail (cf->mdir = mdir_lookup(path)) return;
	cf->displayed = true;	// TODO: save user prefs somewhere
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

static void cal_folder_del_events(struct cal_folder *cf)
{
	struct cal_event *ce, *tmp;
	LIST_FOREACH_SAFE(ce, &cal_events, entry, tmp) {
		if (ce->folder == cf) cal_event_del(ce);
	}
}

#if 0
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
	(void)mdir;
	if (action != MDIR_ADD) return;
	struct cal_folder *cf = (struct cal_folder *)data;
	mdir_version version = new ? 0 : param.version;
	struct cal_date start, stop;
	char const *start_str = header_search(header, SCAMBIO_START);
	if (start_str) {
		cal_date_ctor_from_str(&start, start_str);
		on_error return;
	} else {
		error("Invalid calendar message with no "SCAMBIO_START" field");
		return;
	}
	char const *stop_str = header_search(header, SCAMBIO_STOP);
	if (stop_str) {
		cal_date_ctor_from_str(&stop, stop_str);
		on_error {
			cal_date_dtor(&start);
			return;
		}
	} else {
		cal_date_ctor(&stop, 0, 0, 0, 0, 0);
	}
	char const *desc = header_search(header, SCAMBIO_DESCR_FIELD);
	(void)cal_event_new(cf, &start, &stop, desc, version);
	on_error error_clear();	// forget this event, go ahead with others
	cal_date_dtor(&stop);
	cal_date_dtor(&start);
}

static void refresh_folder(struct cal_folder *cf)
{
	cal_folder_del_events(cf);
	on_error return;
	mdir_patch_list(cf->mdir, false, add_event_cb, cf);
}

void refresh(void)
{
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
