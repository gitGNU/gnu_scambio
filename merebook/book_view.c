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
#include "scambio.h"
#include "merebook.h"
#include "merelib.h"

enum {
	FIELD_NAME,
	FIELD_CT,
	NB_CT_FIELDS,
};

static GtkWidget *book_combo;
static GtkListStore *ct_store;
static GtkWidget *ct_list;

/*
 * Callbacks
 */

static void view_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	// Retrieve selected row
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ct_list));
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		alert(GTK_MESSAGE_ERROR, "Select a contact to display");
		return;
	}
	GValue gct;
	memset(&gct, 0, sizeof(gct));
	gtk_tree_model_get_value(gtk_tree_view_get_model(GTK_TREE_VIEW(ct_list)), &iter, FIELD_CT, &gct);
	struct contact *ct = g_value_get_pointer(&gct);
	g_value_unset(&gct);
	debug("Viewing contact %s", ct->name);
	(void)make_contact_window(ct);
	on_error {
		alert(GTK_MESSAGE_ERROR, error_str());
		error_clear();
	}
}

static struct book *book_of_entry(unsigned entry)
{
	struct book *book = LIST_FIRST(&books);
	while (--entry) book = LIST_NEXT(book, entry);
	return book;
}

static void add_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	unsigned entry = gtk_combo_box_get_active(GTK_COMBO_BOX(book_combo));
	if (0 == entry) {
		alert(GTK_MESSAGE_ERROR, "Please choose a book first (not \"All\")");
		return;
	}
	struct book *book = book_of_entry(entry);
	debug("Add a contact in book %s", book->name);
	// TODO : a new contact dialog, into which we enter juste the name, then we run the contact_view
	// with a special flag that removes the "quit" button (so that user must save or cancel with confirm
	// message).
}

static void quit_cb(GtkToolButton *button, gpointer user_data)
{
	debug("quit");
	destroy_cb(GTK_WIDGET(button), user_data);
}

static void add_contact(GtkListStore *store, struct contact *ct)
{
	debug("adding contact '%s'", ct->name);
	GtkTreeIter iter;
	gtk_list_store_insert_with_values(store, &iter, G_MAXINT,
		FIELD_NAME, ct->name,
		FIELD_CT, ct,
		-1);
}

static void select_list(GtkComboBox *combo, gpointer user_data)
{
	debug("Changing list");
	GtkListStore *store = (GtkListStore *)user_data;
	gtk_list_store_clear(store);
	unsigned entry = gtk_combo_box_get_active(combo);
	struct contact *ct;
	if (0 == entry) {	// all
		LIST_FOREACH(ct, &contacts, entry) add_contact(store, ct);
	} else {	// a book
		struct book *book = book_of_entry(entry);
		LIST_FOREACH(ct, &book->contacts, book_entry) add_contact(store, ct);
	}
}

void refresh_contact_list(void)
{
	struct book *book;
	LIST_FOREACH(book, &books, entry) {
		refresh(book);
	}
	select_list(GTK_COMBO_BOX(book_combo), ct_store);
}

static void refresh_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	(void)user_data;
	refresh_contact_list();
}

/*
 * Make window
 */

static GtkWidget *make_contacts_list(void)
{
	// First a book selector
	book_combo = gtk_combo_box_new_text();
	gtk_combo_box_append_text(GTK_COMBO_BOX(book_combo), "All");
	struct book *book;
	LIST_FOREACH(book, &books, entry) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(book_combo), book->name);
	}
	// Then the list of contacts (sorted according to names)
	ct_store = gtk_list_store_new(NB_CT_FIELDS, G_TYPE_STRING, G_TYPE_POINTER);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(ct_store), FIELD_NAME, GTK_SORT_ASCENDING);
	ct_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ct_store));
	g_object_unref(G_OBJECT(ct_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ct_list), FALSE);
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", FIELD_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ct_list), column);
	// All this packed into a vbox
	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), book_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), ct_list, TRUE, TRUE, 0);
	// Select "All" by default, which reloads the list
	g_signal_connect(G_OBJECT(book_combo), "changed", G_CALLBACK(select_list), ct_store);
	gtk_combo_box_set_active(GTK_COMBO_BOX(book_combo), 0);
	return vbox;
}

GtkWidget *make_book_window(void)
{
	GtkWidget *win = make_window(destroy_cb, NULL);
	/* The book window is composed of :
	 * - a book selector (ALL/book1/book2/etc)
	 * - a list of all contact in this book list (or global contacts list), sorted
	 * Selecting an entry then popup the contact view (which is editable).
	 */
	GtkWidget *list = make_contacts_list();

	GtkWidget *toolbar = make_toolbar(4,
		GTK_STOCK_OK,      view_cb,    NULL,
		GTK_STOCK_NEW,     add_cb,     NULL,
		GTK_STOCK_REFRESH, refresh_cb, NULL,
		GTK_STOCK_QUIT,    quit_cb,    win);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), list, TRUE, TRUE, 0);
#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(win), toolbar);
#	else
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
#	endif
	gtk_container_add(GTK_CONTAINER(win), vbox);

	return win;
}
