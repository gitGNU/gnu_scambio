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
#include "misc.h"
#include "vadrouille.h"
#include "calendar.h"
#include "cal_date.h"

/*
 * Data Definitions
 */

struct editor {
	struct sc_view view;
	GtkWidget *folder_combo, *start_entry, *stop_entry;
	GtkTextBuffer *descr_buffer;
	mdir_version replaced_version;
	struct mdirb *replaced_dir;
	struct cal_dir_view *cal_dir_view;
};

/*
 * Callbacks
 */

static void editor_dtor(struct editor *editor)
{
	sc_view_dtor(&editor->view);
}

static void editor_del(struct sc_view *view)
{
	struct editor *editor = DOWNCAST(view, view, editor);
	editor_dtor(editor);
	free(editor);
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
	struct editor *editor = (struct editor *)user_data;
	
	// Find the folder
	gint f = gtk_combo_box_get_active(GTK_COMBO_BOX(editor->folder_combo));
	if (f < 0 || f >= (int)editor->cal_dir_view->nb_dirs) {
		alert(GTK_MESSAGE_ERROR, "You must choose a folder");
		return;
	}
	struct mdirb *mdirb = editor->cal_dir_view->dirs[f].mdirb;

	// build a new header
	struct header *h = header_new();
	(void)header_field_new(h, SC_HAVE_READ_FIELD, conf_get_str("SC_USERNAME"));
	on_error return;
	do {
		char date_str[] = "XXXX XX XX XX XX XX";
		struct cal_date from_date, to_date;
		(void)header_field_new(h, SC_TYPE_FIELD, SC_CAL_TYPE);
		// From
		if_fail (cal_date_ctor_from_input(&from_date, gtk_entry_get_text(GTK_ENTRY(editor->start_entry)), NULL)) break;
		if (! cal_date_is_set(&from_date)) {
			alert(GTK_MESSAGE_ERROR, "You must enter a 'From' date");
			break;
		}
		if_fail (cal_date_to_str(&from_date, date_str, sizeof(date_str))) break;
		(void)header_field_new(h, SC_START_FIELD, date_str);
		// To
		if_fail (cal_date_ctor_from_input(&to_date, gtk_entry_get_text(GTK_ENTRY(editor->stop_entry)), &from_date)) break;
		if (cal_date_is_set(&to_date)) {
			if (cal_date_compare(&from_date, &to_date) > 0) with_error(0, "Start date must precede End date") break;
			if_fail (cal_date_to_str(&to_date, date_str, sizeof(date_str))) break;
			(void)header_field_new(h, SC_STOP_FIELD, date_str);
		}
		char *descr = get_serial_text(editor->descr_buffer);
		on_error break;
		if (descr) (void)header_field_new(h, SC_DESCR_FIELD, descr);
		g_free(descr);
		debug("sending patch");
		mdir_patch_request(&mdirb->mdir, MDIR_ADD, h);
	} while (0);
	header_unref(h);
	on_error {
		alert_error();
		return;
	}
	// Now that the new msg is in, remove the replaced one
	if (editor->replaced_version != 0) {
		assert(editor->replaced_dir);
		mdir_del_request(&editor->replaced_dir->mdir, editor->replaced_version);
	}
	gtk_widget_destroy(editor->view.window);
	mdirb_refresh(mdirb);	// refresh this mdirb
}

/*
 * Build the view
 */

struct sc_view *cal_editor_view_new(struct cal_dir_view *cal_dir_view, struct mdirb *replaced_dir, struct cal_date *start, struct cal_date *stop, char const *descr, mdir_version replaced)
{
	struct editor *editor = Malloc(sizeof(*editor));
	editor->replaced_version = replaced;
	editor->replaced_dir = replaced_dir;
	editor->cal_dir_view = cal_dir_view;

	// First the combo to choose the folder from
	editor->folder_combo = gtk_combo_box_new_text();
	for (unsigned i = 0; i < cal_dir_view->nb_dirs; i++) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(editor->folder_combo), cal_dir_view->dirs[i].name);
		if (cal_dir_view->dirs[i].mdirb == replaced_dir) gtk_combo_box_set_active(GTK_COMBO_BOX(editor->folder_combo), i);
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
	
	GtkWidget *window = make_window(WC_EDITOR, NULL, NULL);

	GtkWidget *toolbar = make_toolbar(2,
		GTK_STOCK_APPLY,  send_cb,  editor,
		GTK_STOCK_CANCEL, close_cb, window);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), descr_text, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);
	sc_view_ctor(&editor->view, editor_del, window);

	return &editor->view;
}
