#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "meremail.h"
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"

/*
 * Data Definitions
 */

enum folder_store_col {
	FOLDER_STORE_NAME,
	FOLDER_STORE_SIZE,
	NB_FOLDER_STORES
};

static GtkTreeStore *folder_store;

/*
 * Callbacks
 */

static void quit_cb(GtkToolButton *button, gpointer user_data)
{
	debug("quit");
	destroy_cb(GTK_WIDGET(button), user_data);
}

static int make_path_name_rec(char *path, size_t max, GtkTreePath *tp)
{
	debug("path is now : %s", gtk_tree_path_to_string(tp));
	if (gtk_tree_path_get_depth(tp) == 0) return 0;	// we are root and have no name
	// we have a parent : write its name first
	// Fetch what depends on treePath before recursive call
	GtkTreeIter iter;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(folder_store), &iter, tp);
	gtk_tree_path_up(tp);
	int len = make_path_name_rec(path, max, tp);
	// then write our name
	GValue gname;
	memset(&gname, 0, sizeof(gname));
	gtk_tree_model_get_value(GTK_TREE_MODEL(folder_store), &iter, FOLDER_STORE_NAME, &gname);
	assert(G_VALUE_HOLDS_STRING(&gname));
	len += snprintf(path+len, max-len, "/%s", g_value_get_string(&gname));
	g_value_unset(&gname);
	return len;
}

static void enter_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	GtkTreeView *tree = (GtkTreeView *)user_data;
	// Retrieve select row, check there is something in there, and change the view
	GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		puts("No selection");
		return;
	}
	GValue gsize;
	memset(&gsize, 0, sizeof(gsize));
	gtk_tree_model_get_value(GTK_TREE_MODEL(folder_store), &iter, FOLDER_STORE_SIZE, &gsize);
	do {
		assert(G_VALUE_HOLDS_UINT(&gsize));
		unsigned size = g_value_get_uint(&gsize);
		if (size == 0) {
			puts("Cannot select an empty folder");
			break;
		}
		GtkTreePath *treePath = gtk_tree_model_get_path(GTK_TREE_MODEL(folder_store), &iter);
		char path[PATH_MAX];	// As we are going to delete this view and the whole tree
		make_path_name_rec(path, sizeof(path), treePath);
		gtk_tree_path_free(treePath);
		debug("Enter list view in '%s'", path);
		GtkWidget *new_win = make_list_window(path);
		on_error {
			alert(error_str());
			error_clear();
		} else {
			gtk_widget_show_all(new_win);
		}
	} while(0);
	g_value_unset(&gsize);
}

/*
 * Build the view
 */

static void fill_folder_store_rec(struct mdir *mdir, GtkTreeIter *iter);
static void add_subfolder_rec(struct mdir *parent, struct mdir *child, bool synched, char const *name, void *data)
{
	(void)parent;
	(void)synched;	// TODO: display this in a way or another (italic ?)
	// Add this name as a child of the given iterator
	GtkTreeIter *parent_iter = (GtkTreeIter *)data;
	GtkTreeIter iter;
	gtk_tree_store_append(folder_store, &iter, parent_iter);
	gtk_tree_store_set(folder_store, &iter,
		FOLDER_STORE_NAME, name,
		FOLDER_STORE_SIZE, (guint)mdir_size(child),
		-1);
	fill_folder_store_rec(child, &iter);
}

static void fill_folder_store_rec(struct mdir *mdir, GtkTreeIter *iter)
{
	mdir_folder_list(mdir, false, add_subfolder_rec, iter);
}

GtkWidget *make_folder_window(char const *parent)
{
	struct mdir *mdir = mdir_lookup(parent);
	on_error return NULL;

	GtkWidget *window = make_window(destroy_cb);

	// The list of messages
	folder_store = gtk_tree_store_new(NB_FOLDER_STORES, G_TYPE_STRING, G_TYPE_UINT);
	// Fill this store
	fill_folder_store_rec(mdir, NULL);
	on_error return NULL;
	
	GtkWidget *folder_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folder_store));
	g_object_unref(G_OBJECT(folder_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(folder_tree), FALSE);
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Name", renderer,
		"text", FOLDER_STORE_NAME,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(folder_tree), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Size", renderer,
		"text", FOLDER_STORE_SIZE,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(folder_tree), column);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_container_add(GTK_CONTAINER(vbox), folder_tree);
	
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	
	GtkToolItem *button_enter = gtk_tool_button_new_from_stock(GTK_STOCK_OK);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_enter, -1);	// enter list view
	g_signal_connect(G_OBJECT(button_enter), "clicked", G_CALLBACK(enter_cb), GTK_TREE_VIEW(folder_tree));
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_ADD), -1);	// create a new folder
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_DELETE), -1);	// delete a folder
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_tool_button_new_from_stock(GTK_STOCK_REFRESH), -1);	// refresh ?

	GtkToolItem *button_quit = gtk_tool_button_new_from_stock(GTK_STOCK_QUIT);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_quit, -1);
	g_signal_connect(G_OBJECT(button_quit), "clicked", G_CALLBACK(quit_cb), NULL);

	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);

	return window;
}

