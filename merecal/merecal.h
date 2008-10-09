#ifndef MERECAL_H_081008
#define MERECAL_H_081008

#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "merelib.h"

void refresh(void);
GtkWidget *make_cal_window(void);

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
void cal_date_ctor(struct cal_date *cd, guint y, guint M, guint d, guint h, guint m);
int cal_date_compare(struct cal_date const *a, struct cal_date const *b);

extern LIST_HEAD(cal_folders, cal_folder) cal_folders;

struct cal_folder {
	LIST_ENTRY(cal_folder) entry;
	char path[PATH_MAX];
	char *short_name;	// points onto path
	struct mdir *mdir;
	bool displayed;
};

extern LIST_HEAD(cal_events, cal_event) cal_events;

struct cal_event {
	LIST_ENTRY(cal_event) entry;	// cal_events are ordered according to (day, hour)
	struct cal_folder *folder;
	struct cal_date start, stop;
	char *description;
	mdir_version version;	// 0 if no synched yet
};

void foreach_event_between(struct cal_date *start, struct cal_date *stop, void (*cb)(struct cal_event *));

#endif
