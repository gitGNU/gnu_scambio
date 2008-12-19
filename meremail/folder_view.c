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
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "meremail.h"

/*
 * Data Definitions
 */

enum {
	FOLDER_STORE_NAME,
	FOLDER_STORE_SIZE,
	NB_FOLDER_STORES
};

static GtkTreeStore *folder_store;
static struct mdir *root_mdir;

/*
 * Callbacks
 */

static void quit_cb(GtkToolButton *button, gpointer user_data)
{
	debug("quit");
	// TODO: wait for all cnx transfert !!
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

static void new_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	GtkWidget *new_win = make_compose_window(NULL, NULL, NULL);
	on_error {
		alert(GTK_MESSAGE_ERROR, error_str());
		error_clear();
	} else {
		gtk_widget_show_all(new_win);
	}
}

static void enter_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	GtkTreeView *tree = (GtkTreeView *)user_data;
	// Retrieve select row, check there is something in there, and change the view
	GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		error("No selection");
		return;
	}
	GValue gsize;
	memset(&gsize, 0, sizeof(gsize));
	gtk_tree_model_get_value(GTK_TREE_MODEL(folder_store), &iter, FOLDER_STORE_SIZE, &gsize);
	assert(G_VALUE_HOLDS_UINT(&gsize));
	unsigned size = g_value_get_uint(&gsize);
	g_value_unset(&gsize);
	if (size == 0) {
		alert(GTK_MESSAGE_INFO, "No messages in this folder");
		return;
	}
	GtkTreePath *treePath = gtk_tree_model_get_path(GTK_TREE_MODEL(folder_store), &iter);
	char path[PATH_MAX];	// As we are going to delete this view and the whole tree
	make_path_name_rec(path, sizeof(path), treePath);
	gtk_tree_path_free(treePath);
	debug("Enter list view in '%s'", path);
	GtkWidget *new_win = make_list_window(path);
	on_error {
		alert(GTK_MESSAGE_ERROR, error_str());
		error_clear();
	} else {
		gtk_widget_show_all(new_win);
	}
}

static void fill_folder_store_rec(struct mdir *mdir, GtkTreeIter *iter);
static void add_subfolder_rec(struct mdir *parent, struct mdir *child, bool synched, char const *name, void *data)
{
	(void)parent;
	(void)synched;	// TODO: display this in a way or another (italic ?)
	// Refresh this mdir content
	struct maildir *maildir = mdir2maildir(child);
	if_fail (maildir_refresh(maildir)) return;
	// Add this name as a child of the given iterator
	GtkTreeIter *parent_iter = (GtkTreeIter *)data;
	GtkTreeIter iter;
	gtk_tree_store_append(folder_store, &iter, parent_iter);
	gtk_tree_store_set(folder_store, &iter,
		FOLDER_STORE_NAME, name,
		FOLDER_STORE_SIZE, maildir->nb_msgs,
		-1);
	fill_folder_store_rec(child, &iter);
}

static void fill_folder_store_rec(struct mdir *mdir, GtkTreeIter *iter)
{
	mdir_folder_list(mdir, false, add_subfolder_rec, iter);
}

static void refresh_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	gtk_tree_store_clear(folder_store);
	fill_folder_store_rec(root_mdir, NULL);
}

/*
 * Build the view
 */

GtkWidget *make_folder_window(char const *parent)
{
	root_mdir = mdir_lookup(parent);
	on_error return NULL;

	GtkWidget *window = make_window(destroy_cb, NULL);

	// The list of messages
	folder_store = gtk_tree_store_new(NB_FOLDER_STORES, G_TYPE_STRING, G_TYPE_UINT);
	// Fill this store
	if_fail (fill_folder_store_rec(root_mdir, NULL)) return NULL;
	
	GtkWidget *folder_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(folder_store));
#	if TRUE == GTK_CHECK_VERSION(2, 10, 0)
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(folder_tree), TRUE);
#	endif
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(folder_tree), FALSE);
	gtk_tree_view_expand_all(GTK_TREE_VIEW(folder_tree));

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

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), make_scrollable(folder_tree), TRUE, TRUE, 0);
	
	GtkWidget *toolbar = make_toolbar(5,
		GTK_STOCK_OK,      enter_cb,   GTK_TREE_VIEW(folder_tree),
		GTK_STOCK_ADD,     new_cb,     NULL,
		GTK_STOCK_DELETE,  NULL,       NULL,
		GTK_STOCK_REFRESH, refresh_cb, NULL,
		GTK_STOCK_QUIT,    quit_cb,    NULL);

	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

	return window;
}

