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
	FIELD_VERSION,
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
	TAILQ_INSERT_TAIL(day2events+day, d2e, entry);
}

static void day2event_del(struct day2event *d2e, unsigned day)
{
	TAILQ_REMOVE(day2events+day, d2e, entry);
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
			FIELD_VERSION, ce->version,
			-1);
}

// month or day changed, so we must refresh the event list for that day
static void reset_day(void)
{
	// Clear the list store
	gtk_list_store_clear(GTK_LIST_STORE(event_store));
	// Add new ones
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	debug("new day : %u %u %u", year, month+1, day);
	struct day2event *d2e;
	TAILQ_FOREACH(d2e, day2events+day, entry) {
		display_event(d2e->event);
	}
}

static void select_event(struct cal_event *ce)
{
	debug("event '%s' selected", ce->description);
	struct cal_date *end = cal_date_is_set(&ce->stop) ? &ce->stop : &ce->start;
	for (
		struct cal_date period = ce->start;
		cal_date_compare(&period, end) <= 0 && period.day <= 31;
		period.day ++
	) {
		gtk_calendar_mark_day(GTK_CALENDAR(calendar), period.day);
		day2event_new(ce, period.day);
		debug("  and added to day %u", period.day);
	}
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
		while (NULL != (d2e = TAILQ_FIRST(day2events+i))) day2event_del(d2e, i);
	}
	// Rebuild
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	struct cal_date month_start, month_stop;
	cal_date_ctor(&month_start, year, month, 1, 0, 0);
	cal_date_ctor(&month_stop,  year, month, 31, 23, 29);
	foreach_event_between(&month_start, &month_stop, select_event);
	cal_date_dtor(&month_start);
	cal_date_dtor(&month_stop);
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

static void new_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	debug("new");
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	struct cal_date start, stop;
	cal_date_ctor(&start, year, month, day, 99, 99);
	cal_date_ctor(&stop,  0, month, day, 99, 99);
	GtkWidget *win = make_edit_window(LIST_FIRST(&cal_folders), &start, &stop, "(edit)");
	gtk_widget_show_all(win);
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
	
	event_store = gtk_list_store_new(NB_STORE_FIELDS, G_TYPE_STRING, G_TYPE_STRING, MDIR_VERSION_G_TYPE);
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
	gtk_tree_view_append_column(GTK_TREE_VIEW(event_list), column);
	
	calendar = gtk_calendar_new();
	GtkCalendarDisplayOptions flags = GTK_CALENDAR_SHOW_HEADING;
#	ifdef HAVE_CAL_DETAILS
	flags |= GTK_CALENDAR_SHOW_DETAILS;
	gtk_calendar_set_detail_func(GTK_CALENDAR(calendar), cal_details, NULL, NULL);
#	endif
	gtk_calendar_set_display_options(GTK_CALENDAR(calendar), flags);
	g_signal_connect(G_OBJECT(calendar), "day-selected",  G_CALLBACK(day_changed_cb), NULL);
	g_signal_connect(G_OBJECT(calendar), "month-changed", G_CALLBACK(month_changed_cb), NULL);

	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH), -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_FIND), -1);	// Choose amongst available calendars (folders)
	GtkToolItem *button_new = gtk_tool_button_new_from_stock(GTK_STOCK_NEW);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_new, -1);
	g_signal_connect(G_OBJECT(button_new), "clicked", G_CALLBACK(new_cb), NULL);
	GtkToolItem *button_close = gtk_tool_button_new_from_stock(GTK_STOCK_QUIT);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_close, -1);
	g_signal_connect(G_OBJECT(button_close), "clicked", G_CALLBACK(quit_cb), window);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vbox), event_list);
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_container_add(GTK_CONTAINER(vbox), calendar);
	gtk_box_set_child_packing(GTK_BOX(vbox), calendar, FALSE, TRUE, 1, GTK_PACK_END);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	reset_month();	// mark the days for wich we have something

	return window;
}

