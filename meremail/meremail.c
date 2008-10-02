#include <stdlib.h>
#include <gtk/gtk.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"

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
	if_fail(mdir_begin()) return;
	if (0 != atexit(mdir_end)) with_error(0, "atexit") return;
}

static GtkWidget *make_UI(void)
{
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "MereMail "VERSION);
	gtk_window_set_default_size(GTK_WINDOW(window), 200, 450);
	//gtk_window_set_default_icon_from_file(PIXMAPS_DIRS "/truc.png", NULL);
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(on_destroy), NULL);
	
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	// The list of messages
	enum msg_store_col {
		MSG_STORE_FROM,
		MSG_STORE_SUBJECT,
		NB_MSG_STORES
	};
	GtkListStore *msg_store = gtk_list_store_new(NB_MSG_STORES, G_TYPE_STRING, G_TYPE_STRING);
	// test content
	GtkTreeIter iter;
	gtk_list_store_insert(msg_store, &iter, 0);
	gtk_list_store_set(msg_store, &iter,
		MSG_STORE_FROM, "pouet@pouet.net",
		MSG_STORE_SUBJECT, "blablabla",
		-1);
	gtk_list_store_insert(msg_store, &iter, 1);
	gtk_list_store_set(msg_store, &iter,
		MSG_STORE_FROM, "rose@rouge.org",
		MSG_STORE_SUBJECT, "glop glop pas glop",
		-1);
	GtkWidget *msg_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(msg_store));
	g_object_unref(G_OBJECT(msg_store));
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("From", renderer, "text", MSG_STORE_FROM, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Subject", renderer, "text", MSG_STORE_SUBJECT, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	gtk_container_add(GTK_CONTAINER(vbox), msg_list);
	
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_NEW), -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_DELETE), -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_QUIT), -1);
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);

	return window;
}

int main(int nb_args, char *args[])
{
	if (! pth_init()) return EXIT_FAILURE;
	if_fail(init()) return EXIT_FAILURE;
	gtk_init(&nb_args, &args);
	GtkWidget *window = make_UI();
	gtk_widget_show_all(window);
	gtk_main();
	return EXIT_SUCCESS;
}
