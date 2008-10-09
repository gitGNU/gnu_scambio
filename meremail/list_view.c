#include <string.h>
#include "meremail.h"

/*
 * Data Definitions
 */

enum {
	MSG_STORE_FROM,
	MSG_STORE_SUBJECT,
	MSG_STORE_VERSION,
	NB_MSG_STORES
};

/*
 * Callbacks
 */

static void close_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	debug("close");
	GtkWidget *window = (GtkWidget *)user_data;
	gtk_widget_destroy(window);
}

/*
 * Build the view
 */

static void add_patch_to_store(struct mdir *mdir, struct header *header, enum mdir_action action, bool new, union mdir_list_param param, void *data)
{
	if (action != MDIR_ADD) return;
	char const *from = header_search(header, SCAMBIO_FROM_FIELD);
	char const *subject = header_search(header, SCAMBIO_DESCR_FIELD);
	if (! from || ! subject) return;	// not an email
	
	(void)mdir;
	(void)new;	// TODO: add an icon to represent this
	GtkListStore *msg_store = (GtkListStore *)data;
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(msg_store, &iter, G_MAXINT,
		MSG_STORE_FROM, from,
		MSG_STORE_SUBJECT, subject,
		MSG_STORE_VERSION, new ? 0 : param.version,	// 0 is never a valid version number
		-1);
}

GtkWidget *make_list_window(char const *folder)
{
	struct mdir *mdir = mdir_lookup(folder);
	on_error return NULL;

	GtkWidget *window = make_window(NULL);

	// The list of messages
	GtkListStore *msg_store = gtk_list_store_new(NB_MSG_STORES, G_TYPE_STRING, G_TYPE_STRING, MDIR_VERSION_G_TYPE);
	// Fill this store
	mdir_patch_list(mdir, false, add_patch_to_store, msg_store);
	
	GtkWidget *msg_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(msg_store));
	g_object_unref(G_OBJECT(msg_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(msg_list), FALSE);
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("From", renderer,
		"text", MSG_STORE_FROM,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Subject", renderer,
		"text", MSG_STORE_SUBJECT,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_container_add(GTK_CONTAINER(vbox), msg_list);
	
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_DIRECTORY), -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_EDIT), -1);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_JUMP_TO), -1);	// poor man's forward
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_DELETE), -1);	// the confirmation will ask if this is a spam and report it accordingly
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_FIND), -1);

	GtkToolItem *button_close = gtk_tool_button_new_from_stock(GTK_STOCK_QUIT);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_close, -1);
	g_signal_connect(G_OBJECT(button_close), "clicked", G_CALLBACK(close_cb), window);

	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);

	return window;
}

