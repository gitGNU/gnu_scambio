#include <string.h>
#include <time.h>
#include "merecal.h"

/*
 * Data Definitions
 */

#if GTK_CHECK_VERSION(2,14,0)
#	define HAVE_CAL_DETAILS
#endif

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

GtkWidget *make_cal_window(unsigned nb_folders, char const *folders[])
{
	GtkWidget *window = make_window(NULL);
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	
	GtkWidget *calendar = gtk_calendar_new();
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	gtk_calendar_select_month(GTK_CALENDAR(calendar), tm->tm_mon, tm->tm_year + 1900);
	gtk_calendar_mark_day(GTK_CALENDAR(calendar), tm->tm_mday);
	gtk_calendar_set_display_options(GTK_CALENDAR(calendar), GTK_CALENDAR_SHOW_HEADING/*|GTK_CALENDAR_SHOW_DETAILS*/);
#	ifdef HAVE_CAL_DETAILS
	gtk_calendar_set_detail_func(GTK_CALENDAR(calendar), cal_details, NULL, NULL);
	gtk_container_add(GTK_CONTAINER(vbox), calendar);
#	endif

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
	return window;
}

