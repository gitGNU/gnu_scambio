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
#include <time.h>
#include <assert.h>
#include "merecal.h"

/*
 * Data Definitions
 */

struct editor {
	GtkWidget *window, *folder_combo, *start_entry, *stop_entry;
	GtkTextBuffer *descr_buffer;
	mdir_version replaced;
};

/*
 * Callbacks
 */

static void editor_del(struct editor *e)
{
	gtk_widget_destroy(e->window);
	free(e);
}

static void close_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	debug("close");
	editor_del((struct editor *)user_data);
}

static char *get_serial_text(GtkTextBuffer *buffer)
{
	GtkTextIter begin, end;
	gtk_text_buffer_get_start_iter(buffer, &begin);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gchar *descr = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
	size_t len = strlen(descr);
	if (len == 0) {
		free(descr);
		return NULL;
	} else if (len > 2000) {	// FIXME
		alert(GTK_MESSAGE_ERROR, "Abusing event description");
		free(descr);
		with_error(0, "Descr too long") return NULL;
	} else {
		for (unsigned c=0; c<len; c++) if (descr[c] == '\n') descr[c] = ' ';
		return descr;
	}
}

static void send_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	assert(! is_error());
	struct editor *e = (struct editor *)user_data;
	// Find the folder
	gint f = gtk_combo_box_get_active(GTK_COMBO_BOX(e->folder_combo));
	if (f == -1) {
		alert(GTK_MESSAGE_ERROR, "You must choose a folder");
		return;
	}
	struct cal_folder *cf;
	LIST_FOREACH(cf, &cal_folders, entry) {
		if (0 == f--) break;
	}
	assert(cf);
	// build a new header
	struct header *h = header_new();
	on_error return;
	do {
		char date_str[] = "XXXX XX XX XX XX XX";
		struct cal_date date;
		if_fail (header_add_field(h, SC_TYPE_FIELD, SC_CAL_TYPE)) break;
		// From
		if_fail (cal_date_ctor_from_input(&date, gtk_entry_get_text(GTK_ENTRY(e->start_entry)))) break;
		if (! cal_date_is_set(&date)) {
			alert(GTK_MESSAGE_ERROR, "You must enter a 'From' date");
			break;
		}
		if_fail (cal_date_to_str(&date, date_str, sizeof(date_str))) break;
		if_fail (header_add_field(h, SC_START_FIELD, date_str)) break;
		// To
		if_fail (cal_date_ctor_from_input(&date, gtk_entry_get_text(GTK_ENTRY(e->stop_entry)))) break;
		if (cal_date_is_set(&date)) {
			if_fail (cal_date_to_str(&date, date_str, sizeof(date_str))) break;
			if_fail (header_add_field(h, SC_STOP_FIELD, date_str)) break;
		}
		char *descr = get_serial_text(e->descr_buffer);
		on_error break;
		if (descr) header_add_field(h, SC_DESCR_FIELD, descr);
		debug("sending patch");
		mdir_patch_request(cf->mdir, MDIR_ADD, h);
	} while (0);
	header_del(h);
	unless_error {
		// Now that the new msg is in, remove the replaced one
		if (e->replaced != 0) {
			mdir_del_request(cf->mdir, e->replaced);
		}
		editor_del(e);
	}
}

/*
 * Build the view
 */

GtkWidget *make_edit_window(struct cal_folder *default_cf, struct cal_date *start, struct cal_date *stop, char const *descr, mdir_version replaced)
{
	struct editor *editor = malloc(sizeof(*editor));
	if (! editor) with_error(ENOMEM, "malloc editor") return NULL;
	editor->replaced = replaced;

	editor->window = make_window(NULL);

	// First the combo to choose the folder from
	editor->folder_combo = gtk_combo_box_new_text();
	struct cal_folder *cf;
	int i = 0;
	LIST_FOREACH(cf, &cal_folders, entry) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(editor->folder_combo), cf->name);
		if (cf == default_cf) gtk_combo_box_set_active(GTK_COMBO_BOX(editor->folder_combo), i);
		i++;
	}
	
	// Then two date editors (text inputs), the second one being optional
	editor->start_entry = gtk_entry_new_with_max_length(100);
	gtk_entry_set_text(GTK_ENTRY(editor->start_entry), start->str);
	editor->stop_entry = gtk_entry_new_with_max_length(100);
	gtk_entry_set_text(GTK_ENTRY(editor->stop_entry), stop->str);

	GtkWidget *table = make_labeled_hboxes(3,
		"Folder :", editor->folder_combo,
		"From :", editor->start_entry,
		"To :", editor->stop_entry);
	
	// Then a text editor for the description
	GtkWidget *descr_text = gtk_text_view_new();
	editor->descr_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(descr_text));
	gtk_text_buffer_set_text(editor->descr_buffer, descr, -1);
	
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	GtkToolItem *button_cancel = gtk_tool_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_cancel, -1);
	g_signal_connect(G_OBJECT(button_cancel), "clicked", G_CALLBACK(close_cb), editor);
	GtkToolItem *button_ok = gtk_tool_button_new_from_stock(GTK_STOCK_APPLY);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_ok, -1);
	g_signal_connect(G_OBJECT(button_ok), "clicked", G_CALLBACK(send_cb), editor);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vbox), table);
	gtk_container_add(GTK_CONTAINER(vbox), descr_text);
#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(window), toolbar);
#	else
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);
#	endif
	gtk_box_set_child_packing(GTK_BOX(vbox), table, FALSE, TRUE, 1, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(vbox), descr_text, TRUE, TRUE, 1, GTK_PACK_START);

	gtk_container_add(GTK_CONTAINER(editor->window), vbox);
	return editor->window;
}

