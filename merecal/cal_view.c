#include <string.h>
#include <time.h>
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

/*
 * Content
 */

static void add_event(struct cal_event *ce)
{
	(void)ce;	// TODO
}

// month or day changed, so we must refresh the event list for that day
static void reset_day(void)
{
	// TODO: clear the list store
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	struct cal_date day_start, day_stop;
	cal_date_ctor(&day_start, year, month, 1, 0, 0);
	cal_date_ctor(&day_stop,  year, month, 31, 23, 29);
	foreach_event_between(&day_start, &day_stop, add_event);
	cal_date_dtor(&day_start);
	cal_date_dtor(&day_stop);
}

static void mark_days(struct cal_event *ce)
{
	struct cal_date *end = cal_date_is_set(&ce->stop) ? &ce->stop : &ce->start;
	struct cal_date period = ce->start;
	while (cal_date_compare(&period, end) <= 0 && period.day <= 31) {
		gtk_calendar_mark_day(GTK_CALENDAR(calendar), period.day ++);
	}
}

// month changed (or the calendar was just created) : mark the correct days
// and display the events for the new selected day
static void reset_month(void)
{
	gtk_calendar_clear_marks(GTK_CALENDAR(calendar));
	guint year, month, day;
	gtk_calendar_get_date(GTK_CALENDAR(calendar), &year, &month, &day);
	struct cal_date month_start, month_stop;
	cal_date_ctor(&month_start, year, month, 1, 0, 0);
	cal_date_ctor(&month_stop,  year, month, 31, 23, 29);
	foreach_event_between(&month_start, &month_stop, mark_days);
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
	GtkWidget *window = make_window(NULL);
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	
	GtkListStore *event_store = gtk_list_store_new(NB_STORE_FIELDS, G_TYPE_STRING, G_TYPE_STRING, MDIR_VERSION_G_TYPE);
	GtkWidget *event_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(event_store));
	g_object_unref(G_OBJECT(event_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(event_list), FALSE);
	
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Hour", renderer,
		"text", FIELD_HOUR, NULL);
	g_object_unref(G_OBJECT(renderer));
	gtk_tree_view_append_column(GTK_TREE_VIEW(event_list), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Description", renderer,
		"text", FIELD_TEXT, NULL);
	g_object_unref(G_OBJECT(renderer));
	gtk_tree_view_append_column(GTK_TREE_VIEW(event_list), column);
	gtk_container_add(GTK_CONTAINER(vbox), event_list);
	
	calendar = gtk_calendar_new();
	GtkCalendarDisplayOptions flags = GTK_CALENDAR_SHOW_HEADING;
#	ifdef HAVE_CAL_DETAILS
	flags |= GTK_CALENDAR_SHOW_DETAILS;
	gtk_calendar_set_detail_func(GTK_CALENDAR(calendar), cal_details, NULL, NULL);
#	endif
	gtk_calendar_set_display_options(GTK_CALENDAR(calendar), flags);
	gtk_container_add(GTK_CONTAINER(vbox), calendar);

	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH), -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_FIND), -1);	// Choose amongst available calendars (folders)
	GtkToolItem *button_close = gtk_tool_button_new_from_stock(GTK_STOCK_QUIT);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_close, -1);
	g_signal_connect(G_OBJECT(button_close), "clicked", G_CALLBACK(quit_cb), window);

	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	reset_month();	// mark the days for wich we have something

	return window;
}

