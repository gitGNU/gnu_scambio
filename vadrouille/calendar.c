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
#include <assert.h>
#include <string.h>
#include "scambio.h"
#include "scambio/timetools.h"
#include "misc.h"
#include "calendar.h"
#include "cal_date.h"
#include "vadrouille.h"

/*
 * Event Message
 */

struct cal_msg {	// ie an event
	struct sc_msg msg;
	LIST_ENTRY(cal_msg) entry;	// cal_events are ordered according to (day, hour)
	struct cal_date start, stop;
	char *descr;
};

static inline struct cal_msg *msg2cmsg(struct sc_msg *msg)
{
	return DOWNCAST(msg, msg, cal_msg);
}

static LIST_HEAD(cal_msgs, cal_msg) cal_msgs; // sorted by start date (single list for all folders)

// insert in the event list, ordered by start date
static void cal_msg_insert(struct cal_msg *cmsg)
{
	struct cal_msg *m;
	LIST_FOREACH(m, &cal_msgs, entry) {
		if (cal_date_compare(&m->start, &cmsg->start) > 0) {
			debug("insert '%s' before '%s'", cmsg->descr, m->descr);
			LIST_INSERT_BEFORE(m, cmsg, entry);
			return;
		} else if (! LIST_NEXT(m, entry)) {
			debug("insert '%s' after last one '%s'", cmsg->descr, m->descr);
			LIST_INSERT_AFTER(m, cmsg, entry);
			return;
		}
	}
	debug("insert '%s' as sole", cmsg->descr);
	LIST_INSERT_HEAD(&cal_msgs, cmsg, entry);
}

static struct sc_plugin plugin;
static void cal_msg_ctor(struct cal_msg *cmsg, struct mdirb *mdirb, struct header *h, mdir_version version)
{
	debug("msg version %"PRIversion, version);

	// To be a cal message, a new patch must have a start field, optionaly a stop and descr
	struct header_field *start = header_find(h, SC_START_FIELD, NULL);
	if (! start) with_error(0, "Not a cal event") return;
	if_fail (cal_date_ctor_from_str(&cmsg->start, start->value)) return;
	struct header_field *stop = header_find(h, SC_STOP_FIELD, NULL);
	if (stop) {
		if_fail (cal_date_ctor_from_str(&cmsg->stop, stop->value)) {
			cal_date_dtor(&cmsg->start);
			return;
		}
		if (cal_date_compare(&cmsg->start, &cmsg->stop) > 0) with_error(0, "event stop > start") {
			cal_date_dtor(&cmsg->start);
			cal_date_dtor(&cmsg->stop);
			return;
		}
	} else {
		cal_date_ctor(&cmsg->stop, 0, 0, 0, 0, 0);
	}
	struct header_field *descr = header_find(h, SC_DESCR_FIELD, NULL);
	cmsg->descr = Strdup(descr ? descr->value : "");
	cal_msg_insert(cmsg);
	sc_msg_ctor(&cmsg->msg, mdirb, h, version, &plugin);
}

static struct sc_msg *cal_msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct cal_msg *cmsg = Malloc(sizeof(*cmsg));
	if_fail (cal_msg_ctor(cmsg, mdirb, h, version)) {
		free(cmsg);
		return NULL;
	}
	return &cmsg->msg;
}

static void cal_msg_dtor(struct cal_msg *cmsg)
{
	debug("cmsg '%s'", cmsg->descr);
	LIST_REMOVE(cmsg, entry);
	cal_date_dtor(&cmsg->start);
	cal_date_dtor(&cmsg->stop);
	FreeIfSet(&cmsg->descr);
	sc_msg_dtor(&cmsg->msg);
}

static void cal_msg_del(struct sc_msg *msg)
{
	struct cal_msg *cmsg = msg2cmsg(msg);
	cal_msg_dtor(cmsg);
	free(cmsg);
}

static char *cal_msg_descr(struct sc_msg *msg)
{
	struct cal_msg *cmsg = msg2cmsg(msg);
	if (! cal_date_has_time(&cmsg->start)) {
		return g_markup_printf_escaped("%s", cmsg->descr);
	}
	char hour[5+3+5+1];
	int len = snprintf(hour, sizeof(hour), "%02uh%02u", (unsigned)cmsg->start.hour, (unsigned)cmsg->start.min);
	if (cal_date_has_time(&cmsg->stop)) {
		snprintf(hour+len, sizeof(hour)-len, " - %02uh%02u", (unsigned)cmsg->stop.hour, (unsigned)cmsg->stop.min);
	}
	return g_markup_printf_escaped("<b>%s</b> : %s", hour, cmsg->descr);
}

/*
 * Directory View
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

/* The dir view shows a month at a time using a casual GtkCalendar, and the event
 * list for the selected day in this month. We use these to map each month day to an
 * event list (tailqueue).
 */

struct day2event {
	TAILQ_ENTRY(day2event) entry;
	struct cal_msg *cmsg;
};

static struct cal_dir_view *global_view;

static void day2event_new(struct cal_dir_view *view, struct cal_msg *cmsg, unsigned day)
{
	struct day2event *d2e = Malloc(sizeof(*d2e));
	d2e->cmsg = cmsg;
	assert(day > 0 && day-1 < sizeof_array(view->day2events));
	TAILQ_INSERT_TAIL(view->day2events+day-1, d2e, entry);
}

static void day2event_del(struct day2event *d2e, struct cal_dir_view *view, unsigned day)
{
	assert(day > 0 && day-1 < sizeof_array(view->day2events));
	TAILQ_REMOVE(view->day2events+day-1, d2e, entry);
	free(d2e);
}

static void day2event_del_all(struct cal_dir_view *view)
{
	struct day2event *d2e;
	for (unsigned i = 0; i < sizeof_array(view->day2events); i++) {
		while (NULL != (d2e = TAILQ_FIRST(view->day2events+i))) {
			day2event_del(d2e, view, i+1);
		}
	}
}

static void display_event(struct cal_msg *cmsg, struct cal_dir_view *view)
{
	char hour[5+3+5+1];
	if (! cal_date_has_time(&cmsg->start)) {
		hour[0] = '\0';
	} else {
		int len = snprintf(hour, sizeof(hour), "%02uh%02u", (unsigned)cmsg->start.hour, (unsigned)cmsg->start.min);
		if (cal_date_has_time(&cmsg->stop)) {
			snprintf(hour+len, sizeof(hour)-len, " - %02uh%02u", (unsigned)cmsg->stop.hour, (unsigned)cmsg->stop.min);
		}
	}
	debug("adding event '%s' : '%s'", hour, cmsg->descr);
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(view->event_store, &iter, G_MAXINT,
		FIELD_HOUR, hour,
		FIELD_TEXT, cmsg->descr,
		FIELD_FOLDER, mdirb_name(cmsg->msg.mdirb),
		FIELD_EVENT, cmsg,
		-1);
}

static void display_now(struct cal_date *cd, struct cal_dir_view *view)
{
	(void)cd;
	debug("adding mark for current time");
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(view->event_store, &iter, G_MAXINT,
		FIELD_HOUR, "-- now --",
		FIELD_TEXT, "",
		FIELD_FOLDER, "",
		FIELD_EVENT, NULL,
		-1);
}

// month or day changed, so we must refresh the event list for that day
static void reset_day(struct cal_dir_view *view)
{
	// Clear the list store
	gtk_list_store_clear(view->event_store);
	// Get current date and time
	struct cal_date now;
	time_t now_ts = time(NULL);
	cal_date_ctor_from_tm(&now, localtime(&now_ts));
	// Add new ones
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(view->calendar), &year, &month, &day);
	bool today = year == now.year && month == now.month && day == now.day;
	debug("new day : %u %u %u %s", year, month+1, day, today ? "(today)":"");
	struct day2event *d2e;
	assert(day > 0 && day-1 < sizeof_array(view->day2events));
	struct cal_date *prev = NULL;
	TAILQ_FOREACH(d2e, view->day2events+day-1, entry) {
		if (today && (!prev || cal_date_compare(&now, prev) > 0) && cal_date_compare(&now, &d2e->cmsg->start) <= 0) {
			today = false;	// shortcut for next test
			display_now(&now, view);
		}
		display_event(d2e->cmsg, view);
	}
	if (today) display_now(&now, view);
}

static void select_event(struct cal_dir_view *view, struct cal_msg *cmsg, void *data)
{
	int *day = data;
	debug("event '%s' selected for day %d", cmsg->descr, *day);
	gtk_calendar_mark_day(GTK_CALENDAR(view->calendar), *day);
	day2event_new(view, cmsg, *day);
}

static bool view_this_dir(struct cal_dir_view *view, struct mdirb *mdirb)
{
	for (unsigned i = 0; i < view->nb_dirs; i++) {
		if (view->dirs[i].mdirb == mdirb) return true;
	}
	return false;
}

static void foreach_event_in_view_between(struct cal_dir_view *view, struct cal_date *start, struct cal_date *stop, void (*cb)(struct cal_dir_view *, struct cal_msg *, void *), void *data)
{
	struct cal_msg *cmsg;
	LIST_FOREACH(cmsg, &cal_msgs, entry) {
		debug("test event '%s' in between %s and %s", cmsg->descr, start->str, stop->str);
		if (cal_date_compare(&cmsg->start, stop) > 0) {
			debug("start after end");
			break;	// cal_events are sorted by start date
		}
		if (! view_this_dir(view, cmsg->msg.mdirb)) {
			debug(" skip because not in this view");
			continue;
		}
		struct cal_date *end = cal_date_is_set(&cmsg->stop) ? &cmsg->stop : &cmsg->start;
		if (cal_date_compare(end, start) < 0) {
			debug("  skip because end date (%s) before start (%s)", end->str, start->str);
			continue;
		}
		cb(view, cmsg, data);
	}
}

// month changed (or the calendar was just created) : prepare the
// day2events array, and mark the used days
static void reset_month(struct cal_dir_view *view)
{
	debug("new month");
	// Clean per-month datas
	gtk_calendar_clear_marks(GTK_CALENDAR(view->calendar));
	day2event_del_all(view);
	// Rebuild
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(view->calendar), &year, &month, &day);
	int const max_days = month_days(year, month);
	for (int d=1; d <= max_days; d++) {
		struct cal_date day_start, day_stop;
		cal_date_ctor(&day_start, year, month, d, 0, 0);
		cal_date_ctor(&day_stop,  year, month, d, 23, 59);
		foreach_event_in_view_between(view, &day_start, &day_stop, select_event, &d);
		cal_date_dtor(&day_start);
		cal_date_dtor(&day_stop);
	}
	reset_day(view);
}

static void refresh_cb(GtkToolButton *button, gpointer user_data)
{
	debug("refresh");
	(void)button;
	struct cal_dir_view *view = (struct cal_dir_view *)user_data;
	for (unsigned d = 0; d < view->nb_dirs; d++) {
		mdirb_refresh(view->dirs[d].mdirb);
	}
}

static void edit_cb(GtkToolButton *button, gpointer user_data)
{
	struct cal_dir_view *view = (struct cal_dir_view *)user_data;
	bool const new = 0 == strcmp(gtk_tool_button_get_stock_id(button), GTK_STOCK_NEW);
	debug("%s", new ? "new":"edit");
	struct cal_date start, stop;
	char *descr;
	struct mdirb *mdirb;
	mdir_version replaced = 0;
	if (new) {
		guint year, month, day;
		gtk_calendar_get_date(GTK_CALENDAR(view->calendar), &year, &month, &day);
		cal_date_ctor(&start, year, month, day, 99, 99);
		cal_date_ctor(&stop,  0, month, day, 99, 99);
		descr = "";
		assert(view->nb_dirs > 0);
		mdirb = view->dirs[0].mdirb;
	} else {	// Retrieve selected event
		GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->event_list));
		GtkTreeIter iter;
		if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
			alert(GTK_MESSAGE_ERROR, "Select an event to edit");
			return;
		}
		GValue gevent;
		memset(&gevent, 0, sizeof(gevent));
		gtk_tree_model_get_value(GTK_TREE_MODEL(view->event_store), &iter, FIELD_EVENT, &gevent);
		struct cal_msg *cmsg = g_value_get_pointer(&gevent);
		g_value_unset(&gevent);
		if (! cmsg) {
			alert(GTK_MESSAGE_ERROR, "Cannot edit this");	// user try to edit current time mark
			return;
		}
		if (cmsg->msg.version < 0) {
			alert(GTK_MESSAGE_ERROR, "Cannot edit a transient event");
			return;
		}
		replaced = cmsg->msg.version;
		start = cmsg->start;
		stop = cmsg->stop;
		descr = cmsg->descr;
		mdirb = cmsg->msg.mdirb;
	}
	(void)cal_editor_view_new(view, mdirb, &start, &stop, descr, replaced);
}

static void del_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct cal_dir_view *view = (struct cal_dir_view *)user_data;
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->event_list));
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		error("No events selected");
		return;
	}
	GValue gevent;
	memset(&gevent, 0, sizeof(gevent));
	gtk_tree_model_get_value(GTK_TREE_MODEL(view->event_store), &iter, FIELD_EVENT, &gevent);
	struct cal_msg *cmsg = g_value_get_pointer(&gevent);
	g_value_unset(&gevent);
	if (! cmsg) {
		alert(GTK_MESSAGE_ERROR, "Current time mark is not deletable (good try)");
		return;
	}
	if (cmsg->msg.version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot delete a transient event");
		return;
	}
	if (confirm("Delete this event ?")) {
		mdir_del_request(&cmsg->msg.mdirb->mdir, cmsg->msg.version);
		mdirb_refresh(cmsg->msg.mdirb);
		reset_month(view);
	}
}

static void day_changed_cb(GtkCalendar *cal, gpointer user_data)
{
	(void)cal;
	struct cal_dir_view *view = (struct cal_dir_view *)user_data;
	reset_day(view);
}

static void month_changed_cb(GtkCalendar *cal, gpointer user_data)
{
	(void)cal;
	struct cal_dir_view *view = (struct cal_dir_view *)user_data;
	reset_month(view);
}

#ifdef HAVE_CAL_DETAILS
static gchar *cal_details(GtkCalendar *calendar, guint year, guint month, guint day, gpointer user_data)
{
	(void)calendar;
	(void)year;
	(void)month;
	(void)day;
	(void)user_data;
	if (day & 1) return NULL;
	return strdup("glop");
}
#endif

static void dir_listener_cb(struct mdirb_listener *listener, struct mdirb *mdirb)
{
	(void)mdirb;
	struct cal_dir *dir = DOWNCAST(listener, listener, cal_dir);
	struct cal_dir_view *view = dir->view;
	assert(mdirb == dir->mdirb);
	reset_month(view);
}

static void cal_dir_ctor(struct cal_dir *dir, struct cal_dir_view *view, struct mdirb *mdirb)
{
	dir->mdirb = mdirb;
	dir->view = view;
	mdirb_listener_ctor(&dir->listener, mdirb, dir_listener_cb);
}

static void cal_dir_dtor(struct cal_dir *dir)
{
	mdirb_listener_dtor(&dir->listener);
}

static void cal_dir_view_add(struct cal_dir_view *view, struct mdirb *mdirb)
{
	if (view->nb_dirs >= sizeof_array(view->dirs)) {
		with_error(0, "Too many dirs in this calendar") return;
	}
	cal_dir_ctor(view->dirs+view->nb_dirs++, view, mdirb);
	reset_month(view);
}

static void cal_dir_view_ctor(struct cal_dir_view *view, struct mdirb *mdirb)
{
	// Init day2events
	for (unsigned i=0; i<sizeof_array(view->day2events); i++) {
		TAILQ_INIT(view->day2events+i);
	}
	view->nb_dirs = 0;

	view->event_store = gtk_list_store_new(NB_STORE_FIELDS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	view->event_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(view->event_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view->event_list), FALSE);
	
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Hour", renderer,
		"text", FIELD_HOUR, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view->event_list), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Description", renderer,
		"text", FIELD_TEXT, NULL);
	gtk_tree_view_column_set_expand(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view->event_list), column);
	column = gtk_tree_view_column_new_with_attributes("Folder", renderer,
		"text", FIELD_FOLDER, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view->event_list), column);
	
	view->calendar = gtk_calendar_new();
	GtkCalendarDisplayOptions flags = GTK_CALENDAR_SHOW_HEADING|GTK_CALENDAR_SHOW_DAY_NAMES|GTK_CALENDAR_SHOW_WEEK_NUMBERS;
#	ifdef HAVE_CAL_DETAILS
	flags |= GTK_CALENDAR_SHOW_DETAILS;
	gtk_calendar_set_detail_func(GTK_CALENDAR(calendar), cal_details, NULL, NULL);
#	endif
	gtk_calendar_set_display_options(GTK_CALENDAR(view->calendar), flags);
	g_signal_connect(G_OBJECT(view->calendar), "day-selected",  G_CALLBACK(day_changed_cb), view);
	g_signal_connect(G_OBJECT(view->calendar), "month-changed", G_CALLBACK(month_changed_cb), view);

	GtkWidget *window = make_window(WC_MSGLIST, NULL, NULL);
	
	GtkWidget *toolbar = make_toolbar(6,
		GTK_STOCK_REFRESH, refresh_cb, view,
		GTK_STOCK_FIND,    NULL,       NULL,
		GTK_STOCK_NEW,     edit_cb,    view,
		GTK_STOCK_EDIT,    edit_cb,    view,
		GTK_STOCK_DELETE,  del_cb,     view,
		GTK_STOCK_QUIT,    close_cb,   window);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), view->event_list, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), view->calendar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	sc_dir_view_ctor(&view->view, &plugin, mdirb, window);
	cal_dir_view_add(view, mdirb);
	assert(!is_error());
}

static struct sc_dir_view *cal_dir_view_new(struct mdirb *mdirb)
{
	struct cal_dir_view *view = Malloc(sizeof(*view));
	if_fail (cal_dir_view_ctor(view, mdirb)) {
		free(view);
		view = NULL;
	}
	return &view->view;
}

static void cal_dir_view_dtor(struct cal_dir_view *view)
{
	while (view->nb_dirs--) cal_dir_dtor(view->dirs + view->nb_dirs);
	day2event_del_all(view);
	g_object_unref(G_OBJECT(view->event_store));
	if (global_view == view) global_view = NULL;
	sc_dir_view_dtor(&view->view);
}

static void cal_dir_view_del(struct sc_view *view_)
{
	struct cal_dir_view *view = DOWNCAST(view2dir_view(view_), view, cal_dir_view);
	cal_dir_view_dtor(view);
	free(view);
}

/*
 * Init
 */

static void function_add_to_cal(struct mdirb *mdirb)
{
	if (global_view) {	// add this mdir to the global view
		debug("Add mdirb '%s' to global calendar", mdirb_name(mdirb));
		if (view_this_dir(global_view, mdirb)) {
			alert(GTK_MESSAGE_WARNING, "This directory is already shown");
			return;
		}
		cal_dir_view_add(global_view, mdirb);
	} else {
		struct sc_dir_view *view_;
		if_fail (view_ = cal_dir_view_new(mdirb)) {
			alert(GTK_MESSAGE_ERROR, error_str());
			error_clear();
		} else {
			global_view = DOWNCAST(view_, view, cal_dir_view);
		}
	}
}

static struct sc_plugin_ops const ops = {
	.msg_new          = cal_msg_new,
	.msg_del          = cal_msg_del,
	.msg_descr        = cal_msg_descr,
	.msg_view_new     = NULL,
	.msg_view_del     = NULL,
	.dir_view_new     = cal_dir_view_new,
	.dir_view_del     = cal_dir_view_del,
	.dir_view_refresh = NULL,
};
static struct sc_plugin plugin = {
	.name = "calendar",
	.type = SC_CAL_TYPE,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 1,
	.dir_functions = {
		{ NULL, "Add", function_add_to_cal },
	},
};

void calendar_init(void)
{
	global_view = NULL;
	LIST_INIT(&cal_msgs);
	sc_plugin_register(&plugin);
}

