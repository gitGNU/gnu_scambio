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
#include <unistd.h>
#include <ctype.h>
#include "scambio/channel.h"
#include "scambio/timetools.h"
#include "misc.h"
#include "mime.h"
#include "merelib.h"
#include "vadrouille.h"
#include "dialog.h"
#include "mail.h"
// FIXME: plugin
#include "contact.h"

/*
 * Destruction
 */

static void mail_composer_dtor(struct mail_composer *comp)
{
	FreeIfSet(&comp->reference);
	contact_picker_dtor(&comp->picker);
	sc_view_dtor(&comp->view);
}

static void mail_composer_del(struct sc_view *view)
{
	struct mail_composer *comp = DOWNCAST(view, view, mail_composer);
	mail_composer_dtor(comp);
	free(comp);
}

/*
 * Callbacks
 */

static void cancel_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct mail_composer *comp = (struct mail_composer *)user_data;
	gtk_widget_destroy(comp->view.window);
}

static void send_file(char const *fname, char const *name, char const *type, struct header *header)
{
	debug("fname='%s', name='%s', type='%s'", fname, name, type);
	char resource[PATH_MAX];
	if_fail (chn_send_file_request(&ccnx, fname, resource)) return;
	size_t len = strlen(resource);
	if (name) len += snprintf(resource+len, sizeof(resource)-len, "; name=\"%s\"", name);
	if (type) len += snprintf(resource+len, sizeof(resource)-len, "; type=\"%s\"", type);
	(void)header_field_new(header, SC_RESOURCE_FIELD, resource);
}

static void add_files_and_send(struct mail_composer *comp, struct header *header)
{
	char editor_fname[] = "/tmp/XXXXXX";
	size_t text_len = 0;
	do {	// Make a temp file from the editor text
		int fd = mkstemp(editor_fname);
		if (fd < 0) with_error(errno, "mkstemp(%s)", editor_fname) break;
		debug("outputing editor buffer into '%s'", editor_fname);
		struct varbuf vb;
		varbuf_ctor_from_gtk_text_view(&vb, comp->editor);
		unless_error {
			text_len = vb.used;
			varbuf_write(&vb, fd);
			varbuf_dtor(&vb);
		}
		(void)close(fd);
	} while (0);
	on_error {
		header_unref(header);
		return;
	}
	bool have_content = false;
	// Send this file and add the resource name to the header
	if (text_len > 1) {	// 1 byte is not an edit ! (2 may be 'ok' ?)
		send_file(editor_fname, "msg", "text/plain; charset=utf-8", header);
		have_content = true;
	} else debug("Skip text for being too small");
	// And all attached files
	unless_error for (unsigned p = 0; p < comp->nb_files && !is_error(); p++) {
		char const *content_type = filename2mime_type(comp->files[p].name);
		debug("guessed content-type : %s", content_type);
		send_file(comp->files[p].name, Basename(comp->files[p].name), content_type, header);
		have_content = true;
	}
	(void)unlink(editor_fname);
	debug("Now sending patch");
	unless_error {
		if (have_content) mdir_patch_request(mail_outbox, MDIR_ADD, header);
		else error_push(0, "Be creative, write something !");
	}
}

static void add_dests(struct header *header, char const *dests)
{
	char const *c = dests;
	do {
		if (*c == ',' || *c == '\0') {
			int len = c - dests;
			char to[len + 1];
			memcpy(to, dests, len);
			to[len] = '\0';
			while (isblank(to[len-1])) to[--len] = '\0';
			if (len > 0) (void)header_field_new(header, SC_TO_FIELD, to);
			if (*c == '\0') break;
			while (*c == ',' || isblank(*c)) c++;
			dests = c;
		}
		c++;
	} while (1);
}

// Return a header with just the basic fields (no content)
static struct header *header_new_from_compose(struct mail_composer *comp)
{
	struct header *header = header_new();
	on_error return NULL;
	(void)header_field_new(header, SC_TYPE_FIELD, SC_MAIL_TYPE);
	(void)header_field_new(header, SC_START_FIELD, sc_ts2gmfield(time(NULL), true));
	(void)header_field_new(header, SC_DESCR_FIELD, gtk_entry_get_text(GTK_ENTRY(comp->subject_entry)));
	add_dests(header, gtk_entry_get_text(GTK_ENTRY(comp->to_entry)));
	(void)header_field_new(header, SC_FROM_FIELD, gtk_combo_box_get_active_text(GTK_COMBO_BOX(comp->from_combo)));
	if (comp->reference) (void)header_field_new(header, SC_EXTID_FIELD, comp->reference);
	return header;
}

static void mail_composer_send(struct mail_composer *comp)
{
	// Prepare a new header from common fields
	struct header *header = header_new_from_compose(comp);
	on_error return;
	add_files_and_send(comp, header);
	header_unref(header);
	// Now wait until the upload is complete
	wait_all_tx(&ccnx, GTK_WINDOW(comp->view.window));
}

void send_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct mail_composer *comp = (struct mail_composer *)user_data;
	mail_composer_send(comp);
	on_error {
		alert_error();
	} else {
		gtk_widget_destroy(comp->view.window);
	}
}

void del_file_cb(GtkButton *button, gpointer user_data)
{
	struct mail_composer *comp = (struct mail_composer *)user_data;
	for (unsigned f = 0; f < comp->nb_files; f++) {
		if (button == GTK_BUTTON(comp->files[f].del_button)) {
			debug("Removing file %u (%s) from the list", f, comp->files[f].name);
			gtk_widget_destroy(comp->files[f].hbox);
			for (comp->nb_files--; f < comp->nb_files; f++) {
				comp->files[f] = comp->files[f+1];
			}
			break;
		}
	}
}

void add_file_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct mail_composer *comp = (struct mail_composer *)user_data;
	if (comp->nb_files >= sizeof_array(comp->files)) {
		alert(GTK_MESSAGE_ERROR, "Too many files !");
		return;
	}
	GtkWidget *file_chooser = gtk_file_chooser_dialog_new("Choose a file", GTK_WINDOW(comp->view.window),
		GTK_FILE_CHOOSER_ACTION_OPEN, 
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
		NULL);
	if (gtk_dialog_run(GTK_DIALOG(file_chooser)) == GTK_RESPONSE_ACCEPT) do {
		struct attached_file *const file = comp->files + comp->nb_files;

		// Save the filename
		char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(file_chooser));
		snprintf(file->name, sizeof(file->name), "%s", filename);
		g_free(filename);

		// Add a mention of it on the files_box
		off_t size = filesize_by_name(file->name);
		on_error {
			alert_error();
			break;
		}
		char *file_markup = g_markup_printf_escaped("<b>%s</b> (%lu bytes)", file->name, (unsigned long)size);
		GtkWidget *label = gtk_label_new(NULL);
		gtk_label_set_markup(GTK_LABEL(label), file_markup);
		g_free(file_markup);

		// As well as a delete button
		file->del_button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
		g_signal_connect(G_OBJECT(file->del_button), "clicked", G_CALLBACK(del_file_cb), comp);
		file->hbox = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(file->hbox), label, TRUE, TRUE, 0);
		gtk_box_pack_end(GTK_BOX(file->hbox), file->del_button, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(comp->files_box), file->hbox, FALSE, FALSE, 0);

		gtk_widget_show_all(file->hbox);

		comp->nb_files++;
	} while (0);
	gtk_widget_destroy(file_chooser);
}

void contact_picked_cb(struct contact_picker *picker, struct contact *ct)
{
	struct mail_composer *comp = DOWNCAST(picker, picker, mail_composer);
	struct header_field *hf = header_find(ct->msg.header, "email", NULL);
	if (! hf) {
		alert(GTK_MESSAGE_ERROR, "This contact does not have an email");
		return;
	}
	char *email = NULL;
	if (NULL == header_find(ct->msg.header, "email", hf)) {
		// There is only one email, use it
		email = parameter_suppress(hf->value);
	} else {
		// Choose between several emails
		GtkWidget *combo = gtk_combo_box_new_text();
		do {
			char text[1024];	// FIXME
			char *category = parameter_extract(hf->value, "category");
			char *mail = parameter_suppress(hf->value);
			if (mail) {
				if (category) {
					// Is that more ugly than a GtkTreeModel ?
					for (char *c = category; *c; c++) if (*c == ':' && c[1] == ' ') *c = ' ';
				}
				snprintf(text, sizeof(text), "%s: %s", category ? category : "unknown", mail);
				gtk_combo_box_append_text(GTK_COMBO_BOX(combo), text);
			}
			FreeIfSet(&mail);
			FreeIfSet(&category);
		} while (NULL != (hf = header_find(ct->msg.header, "email", hf)));

		struct sc_dialog *dialog = sc_dialog_new("Choose an email", GTK_WINDOW(comp->view.window), combo);

		if (sc_dialog_accept(dialog)) {
			char *entry = gtk_combo_box_get_active_text(GTK_COMBO_BOX(combo));
			if (entry) {
				for (char *c = entry; *c; c++) {
					if (*c == ':' && c[1] == ' ') {
						email = Strdup(c+2);
						break;
					}
				}
			}
		}

		sc_dialog_del(dialog);
	}

	if (! email) return;

#	if TRUE == GTK_CHECK_VERSION(2, 14, 0)
	gint len = gtk_entry_get_text_length(GTK_ENTRY(comp->to_entry));
#	else
	gint len = strlen(gtk_entry_get_text(GTK_ENTRY(comp->to_entry)));
#	endif
	if (len > 0) {
		gtk_editable_insert_text(GTK_EDITABLE(comp->to_entry), ", ", 2, &len);
		len += 2;
	}
	gtk_editable_insert_text(GTK_EDITABLE(comp->to_entry), email, strlen(email), &len);

	free(email);
}

/*
 * Construction
 */

static void mail_composer_ctor(struct mail_composer *comp, char const *from, char const *to, char const *subject, char const *reference)
{
	if (! mail_outbox) {
		with_error(0, "Cannot compose message when no outbox is configured") return;
	}
	comp->nb_files = 0;

	GtkWidget *window = make_window(WC_EDITOR, NULL, NULL);

	// From : combobox with all accepted from addresses (with from param preselected)
	comp->from_combo = gtk_combo_box_new_text();
	int selected = 0;
	struct header_field *hf = NULL;
	unsigned nb_froms = 0;
	while (NULL != (hf = header_find(mdir_user_header(user), "smtp-from", hf))) {
		if (from && 0 == strcmp(from, hf->value)) selected = nb_froms;
		gtk_combo_box_append_text(GTK_COMBO_BOX(comp->from_combo), hf->value);
		nb_froms++;
	}
	if (nb_froms == 0) with_error(0, "You must configure a from address") return;
	gtk_combo_box_set_active(GTK_COMBO_BOX(comp->from_combo), selected);

	// To : input text
	comp->to_entry = gtk_entry_new();
	if (to) gtk_entry_set_text(GTK_ENTRY(comp->to_entry), to);
	// And a contact picker (if available - check contact_picker symbol presence)
	contact_picker_ctor(&comp->picker, contact_picked_cb, GTK_WINDOW(window));
	GtkWidget *to_box = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(to_box), comp->to_entry, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(to_box), comp->picker.button, FALSE, FALSE, 0);

	// Subject : input text
	comp->subject_entry = gtk_entry_new();
	if (subject) gtk_entry_set_text(GTK_ENTRY(comp->subject_entry), subject);
	
	// - a small text editor (empty, without requote : this is a handheld device)
	//   (later : add text button that launches an external text editor)
	comp->editor = gtk_text_view_new();

	// Then the list of attached files
	comp->files_box = gtk_vbox_new(TRUE, 0);

	// The toolbar
	GtkWidget *toolbar = make_toolbar(3,
		GTK_STOCK_ADD,     add_file_cb, comp,
		GTK_STOCK_JUMP_TO, send_cb, comp,
		GTK_STOCK_CANCEL,  cancel_cb, comp);

	// Pack all this into the window
	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_box_pack_start(GTK_BOX(vbox), make_labeled_hboxes(3,
		"From :", comp->from_combo,
		"To :", to_box,
		"Subject :", comp->subject_entry, NULL), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), comp->editor, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), make_frame("Attached files", comp->files_box), FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

	// Save the reference for the case the mail will be sent
	comp->reference = reference ? Strdup(reference):NULL;	// I really need a better string object

	sc_view_ctor(&comp->view, mail_composer_del, window);
}

struct sc_view *mail_composer_new(char const *from, char const *to, char const *subject, char const *reference)
{
	debug("from=%s, to=%s, subject=%s", from, to, subject);
	struct mail_composer *comp = Malloc(sizeof(*comp));
	if_fail (mail_composer_ctor(comp, from, to, subject, reference)) {
		free(comp);
		return NULL;
	}
	return &comp->view;
}


