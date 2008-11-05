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
#include <stdarg.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "merelib.h"

/*
 * Inits
 */

static void init_conf(void)
{
	conf_set_default_str("MERELIB_LOG_DIR", "/tmp");
	conf_set_default_int("SC_LOG_LEVEL", 3);
}

static void init_log(char const *filename)
{
	if_fail(log_begin(conf_get_str("MERELIB_LOG_DIR"), filename)) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Setting log level to %d", log_level);
}

void init(char const *logfile, int nb_args, char *args[])
{
	if (! pth_init()) with_error(0, "Cannot init PTH") return;
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail(init_conf()) return;
	if_fail(init_log(logfile)) return;
	if_fail(mdir_begin()) return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
	gtk_init(&nb_args, &args);
}

/*
 * Gtk Helpers
 */

void destroy_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
	debug("Quit");
	pth_kill();
	gtk_main_quit();
}

void alert(GtkMessageType type, char const *text)
{
	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE, text);
	g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
	gtk_widget_show_all(dialog);
}

bool confirm(char const *text)
{
	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL, text);
	bool ret = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
	gtk_widget_destroy(dialog);
	return ret;
}

GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer))
{
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "MereMail "VERSION);
	gtk_window_set_default_size(GTK_WINDOW(win), 700, 400);
	//gtk_window_set_default_icon_from_file(PIXMAPS_DIRS "/truc.png", NULL);
	if (cb) g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(cb), NULL);
	return win;
}

GtkWidget *make_labeled_hbox(char const *label_text, GtkWidget *other)
{
	GtkWidget *label = gtk_label_new(label_text);
	GtkWidget *hbox = gtk_hbox_new(FALSE, 3);
	gtk_container_add(GTK_CONTAINER(hbox), label);
	gtk_container_add(GTK_CONTAINER(hbox), other);
	gtk_box_set_child_packing(GTK_BOX(hbox), label, FALSE, FALSE, 1, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(hbox), other, TRUE, TRUE, 1, GTK_PACK_END);
	return hbox;
}

GtkWidget *make_labeled_hboxes(unsigned nb_rows, ...)
{
	va_list ap;
	va_start(ap, nb_rows);
	GtkWidget *table = gtk_table_new(2, nb_rows, FALSE);
	for (unsigned r=0; r<nb_rows; r++) {
		GtkWidget *label = gtk_label_new(va_arg(ap, char const *));
		gtk_table_attach(GTK_TABLE(table), label, 0, 1, r, r+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 1, 1);
		gtk_table_attach(GTK_TABLE(table), va_arg(ap, GtkWidget *), 1, 2, r, r+1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 1, 1);
	}
	va_end(ap);
	return table;
}

GtkWidget *make_toolbar(unsigned nb_buttons, ...)
{
	va_list ap;
	va_start(ap, nb_buttons);
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	while (nb_buttons--) {
		const gchar *stock_id = va_arg(ap, const gchar *);
		void (*cb)(void) = va_arg(ap, void (*)(void));
		gpointer data = va_arg(ap, gpointer);
		GtkToolItem *button = gtk_tool_button_new_from_stock(stock_id);
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button, -1);
		if (cb) g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(cb), data);
	}
	va_end(ap);
	return toolbar;
}
