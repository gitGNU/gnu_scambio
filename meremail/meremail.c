#include <stdlib.h>
#include <stdio.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "meremail.h"

void destroy_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
	debug("Quit");
	pth_kill();
	gtk_main_quit();
}

static void init_conf(void)
{
	conf_set_default_str("MEREMAIL_LOG_DIR", "/tmp");
	conf_set_default_int("MEREMAIL_LOG_LEVEL", 3);
}

static void init_log(void)
{
	if_fail(log_begin(conf_get_str("MEREMAIL_LOG_DIR"), "meremail.log")) return;
	debug("init log");
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("MEREMAIL_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init(void)
{
	error_begin();
	if (0 != atexit(error_end)) with_error(0, "atexit") return;
	if_fail(init_conf()) return;
	if_fail(init_log()) return;
	if_fail(mdir_begin()) return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
}

void alert(char const *text)
{
	// TODO: display me in a box
	puts(text);
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

int main(int nb_args, char *args[])
{
	if (! pth_init()) return EXIT_FAILURE;
	if_fail(init()) return EXIT_FAILURE;
	gtk_init(&nb_args, &args);
	GtkWidget *folder_window = make_folder_window("/");
	if (! folder_window) return EXIT_FAILURE;
	gtk_widget_show_all(folder_window);
	gtk_main();
	return EXIT_SUCCESS;
}
