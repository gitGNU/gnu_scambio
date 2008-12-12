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
#include <time.h>
#include <assert.h>
#include "merecal.h"

/*
 * Data Definitions
 */

#if GTK_CHECK_VERSION(2,14,0)
#	define HAVE_CAL_DETAILS
#endif

enum {
	FIELD_HOUR,
	FIELD_TEXT,
	FIELD_FOLDER,
	FIELD_EVENT,
	NB_STORE_FIELDS
};

static GtkWidget *calendar;
static GtkListStore *event_store;

struct day2event {
	TAILQ_ENTRY(day2event) entry;
	struct cal_event *event;
};

static TAILQ_HEAD(day2events, day2event) day2events[31];

/*
 * Content
 */

static void day2event_new(struct cal_event *ce, unsigned day)
{
	struct day2event *d2e = malloc(sizeof(*d2e));
	if (! d2e) with_error(ENOMEM, "malloc d2e") return;
	d2e->event = ce;
	assert(day > 0 && day-1 < sizeof_array(day2events));
	TAILQ_INSERT_TAIL(day2events+day-1, d2e, entry);
}

static void day2event_del(struct day2event *d2e, unsigned day)
{
	assert(day > 0 && day-1 < sizeof_array(day2events));
	TAILQ_REMOVE(day2events+day-1, d2e, entry);
	free(d2e);
}

static void display_event(struct cal_event *ce)
{
	char hour[5+3+5+1];
	if (! cal_date_has_time(&ce->start)) {
		hour[0] = '\0';
	} else {
		int len = snprintf(hour, sizeof(hour), "%02uh%02u", (unsigned)ce->start.hour, (unsigned)ce->start.min);
		if (cal_date_has_time(&ce->stop)) {
			snprintf(hour+len, sizeof(hour)-len, " - %02uh%02u", (unsigned)ce->stop.hour, (unsigned)ce->stop.min);
		}
	}
	debug("adding event '%s' : '%s'", hour, ce->description);
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(event_store, &iter, G_MAXINT,
		FIELD_HOUR, hour,
		FIELD_TEXT, ce->description,
		FIELD_FOLDER, ce->folder->name,
		FIELD_EVENT, ce,
		-1);
}

static void display_now(struct cal_date *cd)
{
	(void)cd;
	debug("adding mark for current time");
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(event_store, &iter, G_MAXINT,
		FIELD_HOUR, "-- now --",
		FIELD_TEXT, "",
		FIELD_FOLDER, "",
		FIELD_EVENT, NULL,
		-1);
}

// month or day changed, so we must refresh the event list for that day
static void reset_day(void)
{
	// Clear the list store
	gtk_list_store_clear(event_store);
	// Get current date and time
	struct cal_date now;
	time_t now_ts = time(NULL);
	cal_date_ctor_from_tm(&now, localtime(&now_ts));
	// Add new ones
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	bool today = year == now.year && month == now.month && day == now.day;
	debug("new day : %u %u %u %s", year, month+1, day, today ? "(today)":"");
	struct day2event *d2e;
	assert(day > 0 && day-1 < sizeof_array(day2events));
	struct cal_date *prev = NULL;
	TAILQ_FOREACH(d2e, day2events+day-1, entry) {
		if (today && (!prev || cal_date_compare(&now, prev) > 0) && cal_date_compare(&now, &d2e->event->start) <= 0) {
			today = false;	// shortcut for next test
			display_now(&now);
		}
		display_event(d2e->event);
	}
	if (today) display_now(&now);
}

static void select_event(struct cal_event *ce, void *data)
{
	int *day = data;
	debug("event '%s' selected for day %d", ce->description, *day);
	gtk_calendar_mark_day(GTK_CALENDAR(calendar), *day);
	day2event_new(ce, *day);
}

// month changed (or the calendar was just created) : prepare the
// day2events array, and mark the used days
static void reset_month(void)
{
	debug("new month");
	// Clean per-month datas
	gtk_calendar_clear_marks(GTK_CALENDAR(calendar));
	for (unsigned i=0; i<sizeof_array(day2events); i++) {
		struct day2event *d2e;
		while (NULL != (d2e = TAILQ_FIRST(day2events+i))) day2event_del(d2e, i+1);
	}
	// Rebuild
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	int const max_days = month_days(year, month);
	for (int d=1; d <= max_days; d++) {
		struct cal_date day_start, day_stop;
		cal_date_ctor(&day_start, year, month, d, 0, 0);
		cal_date_ctor(&day_stop,  year, month, d, 23, 59);
		foreach_event_between(&day_start, &day_stop, select_event, &d);
		cal_date_dtor(&day_start);
		cal_date_dtor(&day_stop);
	}
	reset_day();
}

/*
 * Callbacks
 */

static void quit_cb(GtkToolButton *button, gpointer user_data)
{
	debug("quit");
	destroy_cb(GTK_WIDGET(button), user_data);
}

static void refresh_cb(GtkToolButton *button, gpointer user_data)
{
	debug("refresh");
	(void)button;
	(void)user_data;
	refresh();
	reset_month();
}

static void edit_cb(GtkToolButton *button, gpointer user_data)
{
	GtkTreeView *list = (GtkTreeView *)user_data;
	bool const new = 0 == strcmp(gtk_tool_button_get_stock_id(button), GTK_STOCK_NEW);
	debug("%s", new ? "new":"edit");
	struct cal_date start, stop;
	char *descr;
	struct cal_folder *cf;
	mdir_version replaced = 0;
	if (new) {
		guint year, month, day;
		gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
		cal_date_ctor(&start, year, month, day, 99, 99);
		cal_date_ctor(&stop,  0, month, day, 99, 99);
		descr = "";
		cf = LIST_FIRST(&cal_folders);
	} else {	// Retrieve selected event
		GtkTreeSelection *selection = gtk_tree_view_get_selection(list);
		GtkTreeIter iter;
		if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
			alert(GTK_MESSAGE_ERROR, "Select an event to display");
			return;
		}
		GValue gevent;
		memset(&gevent, 0, sizeof(gevent));
		gtk_tree_model_get_value(GTK_TREE_MODEL(event_store), &iter, FIELD_EVENT, &gevent);
		struct cal_event *ce = g_value_get_pointer(&gevent);
		g_value_unset(&gevent);
		if (! ce) {
			alert(GTK_MESSAGE_ERROR, "Cannot edit this");	// user try to edit current time mark
			return;
		}
		if (ce->version < 0) {
			alert(GTK_MESSAGE_ERROR, "Cannot edit a transient event");
			return;
		}
		replaced = ce->version;
		start = ce->start;
		stop = ce->stop;
		descr = ce->description;
		cf = ce->folder;
	}
	GtkWidget *win = make_edit_window(cf, &start, &stop, descr, replaced);
	// We want our view to be refreshed once the user quits the editor
	g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(refresh_cb), NULL);
	gtk_widget_show_all(win);
}

static void del_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	GtkTreeView *list = (GtkTreeView *)user_data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection(list);
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		error("No events selected");
		return;
	}
	GValue gevent;
	memset(&gevent, 0, sizeof(gevent));
	gtk_tree_model_get_value(GTK_TREE_MODEL(event_store), &iter, FIELD_EVENT, &gevent);
	struct cal_event *ce = g_value_get_pointer(&gevent);
	g_value_unset(&gevent);
	if (! ce) {
		alert(GTK_MESSAGE_ERROR, "Current time mark is not deletable (good try)");
		return;
	}
	if (ce->version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot delete a transient event");
		return;
	}
	if (confirm("Delete this event ?")) {
		mdir_del_request(ce->folder->mdir, ce->version);
		refresh();
		reset_month();
	}
}

static void day_changed_cb(GtkCalendar *cal, gpointer user_data)
{
	assert(cal == GTK_CALENDAR(calendar));
	(void)user_data;
	reset_day();
}

static void month_changed_cb(GtkCalendar *cal, gpointer user_data)
{
	assert(cal == GTK_CALENDAR(calendar));
	(void)user_data;
	reset_month();
}

/*
 * Build the view
 */

gchar *cal_details(GtkCalendar *calendar, guint year, guint month, guint day, gpointer user_data)
{
	(void)calendar;
	(void)year;
	(void)month;
	(void)day;
	(void)user_data;
	if (day & 1) return NULL;
	return strdup("glop");
}

GtkWidget *make_cal_window(void)
{
	for (unsigned i=0; i<sizeof_array(day2events); i++) {
		TAILQ_INIT(day2events+i);
	}

	GtkWidget *window = make_window(NULL);
	
	event_store = gtk_list_store_new(NB_STORE_FIELDS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	GtkWidget *event_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(event_store));
	g_object_unref(G_OBJECT(event_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(event_list), FALSE);
	
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Hour", renderer,
		"text", FIELD_HOUR, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(event_list), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Description", renderer,
		"text", FIELD_TEXT, NULL);
	gtk_tree_view_column_set_expand(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(event_list), column);
	column = gtk_tree_view_column_new_with_attributes("Folder", renderer,
		"text", FIELD_FOLDER, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(event_list), column);
	
	calendar = gtk_calendar_new();
	GtkCalendarDisplayOptions flags = GTK_CALENDAR_SHOW_HEADING|GTK_CALENDAR_SHOW_DAY_NAMES|GTK_CALENDAR_SHOW_WEEK_NUMBERS;
#	ifdef HAVE_CAL_DETAILS
	flags |= GTK_CALENDAR_SHOW_DETAILS;
	gtk_calendar_set_detail_func(GTK_CALENDAR(calendar), cal_details, NULL, NULL);
#	endif
	gtk_calendar_set_display_options(GTK_CALENDAR(calendar), flags);
	g_signal_connect(G_OBJECT(calendar), "day-selected",  G_CALLBACK(day_changed_cb), NULL);
	g_signal_connect(G_OBJECT(calendar), "month-changed", G_CALLBACK(month_changed_cb), NULL);

	GtkWidget *toolbar = make_toolbar(6,
		GTK_STOCK_REFRESH, refresh_cb, NULL,
		GTK_STOCK_FIND,    NULL,       NULL,
		GTK_STOCK_NEW,     edit_cb,    GTK_TREE_VIEW(event_list),
		GTK_STOCK_EDIT,    edit_cb,    GTK_TREE_VIEW(event_list),
		GTK_STOCK_DELETE,  del_cb,     GTK_TREE_VIEW(event_list),
		GTK_STOCK_QUIT,    quit_cb,    window);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), event_list, TRUE, TRUE, 0);
#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(window), toolbar);
#	else
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
#	endif
	gtk_box_pack_end(GTK_BOX(vbox), calendar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	reset_month();	// mark the days for wich we have something

	return window;
}

