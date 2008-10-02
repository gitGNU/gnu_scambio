#include <stdlib.h>
#include <gtk/gtk.h>
#include <pth.h>
#include "scambio.h"

static void on_destroy(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
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
}

static void make_UI(void)
{
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "MereMail "VERSION);
	gtk_window_set_default_size(GTK_WINDOW(window), 200, 50);
	//gtk_window_set_default_icon_from_file(PIXMAPS_DIRS "/truc.png", NULL);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_destroy), NULL);
	
	GtkWidget *label = gtk_label_new("I compile and run");
	gtk_container_add(GTK_CONTAINER(window), label);
	gtk_widget_show_all(window);
}

int main(int nb_args, char *args[])
{
	if (! pth_init()) return EXIT_FAILURE;
	if_fail(init()) return EXIT_FAILURE;
	gtk_init(&nb_args, &args);
	make_UI();
	gtk_main();
	return EXIT_SUCCESS;
}
