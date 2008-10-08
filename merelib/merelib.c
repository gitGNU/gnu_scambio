#include <stdlib.h>
#include <stdio.h>
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
	conf_set_default_int("MERELIB_LOG_LEVEL", 3);
}

static void init_log(char const *filename)
{
	if_fail(log_begin(conf_get_str("MERELIB_LOG_DIR"), filename)) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("MERELIB_LOG_LEVEL");
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
	GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT/*|GTK_DIALOG_NO_SEPARATOR*/, type, GTK_BUTTONS_CLOSE, text);
	g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
	gtk_widget_show_all(dialog);
}

GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer))
{
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "MereMail "VERSION);
	gtk_window_set_default_size(GTK_WINDOW(win), 200, 450);
	//gtk_window_set_default_icon_from_file(PIXMAPS_DIRS "/truc.png", NULL);
	if (cb) g_signal_connect(G_OBJECT(win), "destroy", G_CALLBACK(cb), NULL);
	return win;
}

