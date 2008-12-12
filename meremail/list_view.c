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
	MSG_STORE_DATE,
	MSG_STORE_MSGPTR,
	NB_MSG_STORES
};

/*
 * Callbacks
 */

static void view_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	// Retrieve selected row
	GtkTreeView *list = (GtkTreeView *)user_data;
	GtkTreeModel *model = gtk_tree_view_get_model(list);
	if (! model) {
		error("Tree without a model?");
		return;
	}
	GtkTreeSelection *selection = gtk_tree_view_get_selection(list);
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		alert(GTK_MESSAGE_ERROR, "Select a message to display");
		return;
	}
	GValue gevent;
	memset(&gevent, 0, sizeof(gevent));
	gtk_tree_model_get_value(model, &iter, MSG_STORE_MSGPTR, &gevent);
	struct msg *msg = g_value_get_pointer(&gevent);
	g_value_unset(&gevent);
	debug("Viewing message %"PRIversion, msg->version);
	GtkWidget *new_win = make_mail_window(msg);
	on_error {
		alert(GTK_MESSAGE_ERROR, error_str());
		error_clear();
	} else {
		gtk_widget_show_all(new_win);
	}
}

/*
 * Build the view
 */

static void fill_store_from_maildir(GtkListStore *store, struct maildir *maildir)
{
	struct msg *msg;
	GtkTreeIter iter;
	LIST_FOREACH (msg, &maildir->msgs, entry) {
		gtk_list_store_insert_with_values(store, &iter, G_MAXINT,
			MSG_STORE_FROM, msg->from,
			MSG_STORE_SUBJECT, msg->descr,
			MSG_STORE_DATE, ts2staticstr(msg->date),
			MSG_STORE_MSGPTR, msg,
			-1);
	}
}

GtkWidget *make_list_window(char const *folder)
{
	struct mdir *mdir = mdir_lookup(folder);
	on_error return NULL;
	struct maildir *maildir = mdir2maildir(mdir);
	maildir_refresh(maildir);	// just in case the mdir was just created (mdir lib may randomly destruct mdir)

	GtkWidget *window = make_window(NULL, NULL);

	// The list of messages
	GtkListStore *msg_store = gtk_list_store_new(NB_MSG_STORES, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	// Fill this store
	fill_store_from_maildir(msg_store, maildir);
	
	GtkWidget *msg_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(msg_store));
	g_object_unref(G_OBJECT(msg_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(msg_list), FALSE);
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

	GtkTreeViewColumn *column;
	column = gtk_tree_view_column_new_with_attributes("Date", renderer,
		"text", MSG_STORE_DATE,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	column = gtk_tree_view_column_new_with_attributes("From", renderer,
		"text", MSG_STORE_FROM,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Subject", renderer,
		"text", MSG_STORE_SUBJECT,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(msg_list), column);
	
	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), make_scrollable(msg_list), TRUE, TRUE, 0);
	
	GtkWidget *toolbar = make_toolbar(5,
		GTK_STOCK_OK,      view_cb,  GTK_TREE_VIEW(msg_list),	// View
		GTK_STOCK_JUMP_TO, NULL,     NULL,	// Forward
		GTK_STOCK_DELETE,  NULL,     NULL,	// Delete
		GTK_STOCK_FIND,    NULL,     NULL,	// Find
		GTK_STOCK_QUIT,    close_cb, window);

#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(window), toolbar);
#	else
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
#	endif
	return window;
}

