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

static void add_patch_to_store(struct mdir *mdir, struct header *header, enum mdir_action action, mdir_version version, void *data)
{
	if (action != MDIR_ADD) return;
	char const *from = header_search(header, SC_FROM_FIELD);
	char const *subject = header_search(header, SC_DESCR_FIELD);
	if (! from || ! subject) return;	// not an email
	
	(void)mdir;
	GtkListStore *msg_store = (GtkListStore *)data;
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(msg_store, &iter, G_MAXINT,
		MSG_STORE_FROM, from,
		MSG_STORE_SUBJECT, subject,
		MSG_STORE_VERSION, version,
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
	mdir_patch_reset(mdir);
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
	
	GtkWidget *toolbar = make_toolbar(5,
		GTK_STOCK_DIRECTORY, NULL, NULL,
		GTK_STOCK_EDIT,      NULL, NULL,
		GTK_STOCK_JUMP_TO,   NULL, NULL,
		GTK_STOCK_DELETE,    NULL, NULL,
		GTK_STOCK_FIND,      NULL, NULL);

	GtkToolItem *button_close = gtk_tool_button_new_from_stock(GTK_STOCK_QUIT);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_close, -1);
	g_signal_connect(G_OBJECT(button_close), "clicked", G_CALLBACK(close_cb), window);

	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);

	return window;
}

