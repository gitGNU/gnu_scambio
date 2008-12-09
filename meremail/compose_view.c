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
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "merelib.h"
#include "meremail.h"
#include "scambio/channel.h"

struct compose {
	GtkWidget *win;
	GtkWidget *from_combo, *to_entry, *subject_entry, *editor;
	unsigned nb_files;
	char filenames[32][PATH_MAX];
};

static void compose_ctor(struct compose *comp, char const *from, char const *to, char const *subject)
{
	comp->win = make_window(NULL);
	// From : combobox with all accepted from addresses (with from param preselected)
	unsigned nb_froms;
	char const **froms = header_search_all(mdir_user_header(user), "smtp-from", &nb_froms);
	on_error return;
	if (nb_froms == 0) with_error(0, "You must configure a from address") return;
	comp->from_combo = gtk_combo_box_new_text();
	int selected = 0;
	for (unsigned f = 0; f < nb_froms; f++) {
		if (from && 0 == strcmp(from, froms[f])) selected = f;
		gtk_combo_box_append_text(GTK_COMBO_BOX(comp->from_combo), froms[f]);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(comp->from_combo), selected);
	free(froms);
	// To : input text (with latter a button to pick a contact)
	comp->to_entry = gtk_entry_new();
	if (to) gtk_entry_set_text(GTK_ENTRY(comp->to_entry), to);
	// Subject : input text
	comp->subject_entry = gtk_entry_new();
	if (subject) gtk_entry_set_text(GTK_ENTRY(comp->subject_entry), subject);
	// - a small text editor (empty, without requote : this is a handheld device)
	//   (later : add text button that launches an external text editor)
	comp->editor = gtk_text_view_new();
}

static struct compose *compose_new(char const *from, char const *to, char const *subject)
{
	struct compose *comp = calloc(1, sizeof(*comp));
	if (! comp) with_error(ENOMEM, "malloc(compose)") return NULL;
	if_fail (compose_ctor(comp, from, to, subject)) {
		free(comp);
		comp = NULL;
	}
	return comp;
}

static void compose_dtor(struct compose *comp)
{
	gtk_widget_destroy(comp->win);
}

static void compose_del(struct compose *comp)
{
	compose_dtor(comp);
	free(comp);
}

static void compose_send(struct compose *comp)
{
	(void)comp;
	error_push(0, "Not implemented yet");
}

/*
 * Callbacks
 */

void cancel_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct compose *comp = (struct compose *)user_data;
	compose_del(comp);
}

void send_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct compose *comp = (struct compose *)user_data;
	compose_send(comp);
	on_error {
		alert(GTK_MESSAGE_ERROR, error_str());
		error_clear();
	} else {
		compose_del(comp);
	}
}

void add_file(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct compose *comp = (struct compose *)user_data;
	if (comp->nb_files >= sizeof_array(comp->filenames)) {
		alert(GTK_MESSAGE_ERROR, "Too many files !");
		return;
	}
	GtkWidget *file_chooser = gtk_file_chooser_dialog_new("Choose a file", GTK_WINDOW(comp->win),
		GTK_FILE_CHOOSER_ACTION_OPEN, 
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
		NULL);
	if (gtk_dialog_run(GTK_DIALOG(file_chooser)) == GTK_RESPONSE_ACCEPT) {
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_chooser));
		snprintf(comp->filenames[comp->nb_files++], sizeof(comp->filenames[0]), "%s", filename);
		g_free(filename);
	}
	gtk_widget_destroy(file_chooser);
}

/*
 * Make a Window to compose a mail, with optional parameters for replying to another mail
 */

GtkWidget *make_compose_window(char const *from, char const *to, char const *subject)
{
	debug("from=%s, to=%s, subject=%s", from, to, subject);
	struct compose *comp = compose_new(from, to, subject);
	on_error return NULL;

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(comp->win), vbox);
	GtkWidget *formH = make_labeled_hboxes(3,
		"From :", comp->from_combo,
	  	"To :", comp->to_entry,
	  	"Subject :", comp->subject_entry, NULL);
	gtk_container_add(GTK_CONTAINER(vbox), formH);
	gtk_box_set_child_packing(GTK_BOX(vbox), formH, FALSE, FALSE, 1, GTK_PACK_START);
	gtk_container_add(GTK_CONTAINER(vbox), comp->editor);
	// Then some buttons :
	// - add file (later an editable list of joined files)
	GtkWidget *add_button = gtk_button_new_with_label("Attach file");
	g_signal_connect(G_OBJECT(add_button), "clicked", G_CALLBACK(add_file), comp);
	gtk_container_add(GTK_CONTAINER(vbox), add_button);
	gtk_box_set_child_packing(GTK_BOX(vbox), add_button, FALSE, FALSE, 1, GTK_PACK_START);
	// - cancel, send
	GtkWidget *toolbar = make_toolbar(2,
		GTK_STOCK_JUMP_TO, send_cb, comp,
		GTK_STOCK_CANCEL,  cancel_cb, comp);

#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(comp->win), toolbar);
#	else
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);
#	endif

	return comp->win;
}
