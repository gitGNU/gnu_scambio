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
#include <glib.h>
#include <gtkhtml/gtkhtml.h>
#include "merelib.h"
#include "meremail.h"
#include "varbuf.h"

/*
 * Make a Window to display a mail
 */

static GtkWidget *make_view_widget(char const *type, char const *resource, GtkWindow *win)
{
	debug("type='%s', resource='%s'", type, resource);
	GtkWidget *widget = NULL;
	struct varbuf vb;
	// First try to get the resource
	char filename[PATH_MAX];
	if_fail (chn_get_file(&ccnx, filename, resource)) {
		widget = gtk_label_new(NULL);
		char *str = g_markup_printf_escaped("<b>Cannot fetch remote resource</b> <i>%s</i> : %s", resource, error_str());
		gtk_label_set_markup(GTK_LABEL(widget), str);
		g_free(str);
		error_clear();	// replace by an error text
		goto q;
	}
	wait_all_tx(&ccnx, win);
	// For text files, display in a text widget (after utf8 conversion)
	// For html, use GtkHtml,
	// For image, display as an image
	// For other types, display a launcher for external app
	if (0 == strncmp(type, "text/html", 9)) {
		if_fail (varbuf_ctor_from_file(&vb, filename)) return NULL;
		widget = gtk_html_new_from_string(vb.buf, vb.used);
		varbuf_dtor(&vb);
	} else if (0 == strncmp(type, "text/", 5)) {	// all other text types are treated the same
		GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
		char charset[64];	// wild guess
		if_fail (header_copy_parameter("charset", type, sizeof(charset), charset)) {
			if (error_code() == ENOENT) {
				error_clear();
				snprintf(charset, sizeof(charset), "us-ascii");
			} else return NULL;
		}
		if_fail (varbuf_ctor_from_file(&vb, filename)) return NULL;
		// convert to UTF-8
		gsize utf8size;
		gchar *utf8text = g_convert_with_fallback(vb.buf, vb.used, "utf-8", charset, NULL, NULL, &utf8size, NULL);
		varbuf_dtor(&vb);
		if (! utf8text) with_error(0, "Cannot convert from '%s' to utf8", charset) return NULL;
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), utf8text, utf8size);
		widget = gtk_text_view_new_with_buffer(buffer);
		g_object_unref(G_OBJECT(buffer));
		g_free(utf8text);
	} else if (0 == strncmp(type, "image/", 6)) {
		widget = gtk_image_new_from_file(filename);	// Oh! All mighty gtk !
	}
	if (! widget) {
		widget = gtk_label_new(NULL);
		char *str = g_markup_printf_escaped("<b>Cannot display this part</b> which have type '<i>%s</i>'", type);
		gtk_label_set_markup(GTK_LABEL(widget), str);
		g_free(str);
	}
q:	return make_scrollable(widget);
}

GtkWidget *make_mail_window(struct msg *msg)
{
	/* The mail view is merely a succession of all the attached files,
	 * preceeded with our mail patch.
	 * It would be great if some of the files be closed by default
	 * (for the first one, the untouched headers, is not of much use).
	 */
	debug("for msg %s", msg->descr);
	GtkWidget *win = make_window(NULL);
	enum mdir_action action;
	struct header *h = mdir_read(&msg->maildir->mdir, msg->version, &action);
	on_error return NULL;
	assert(action == MDIR_ADD);
	unsigned nb_resources;
	char const **resources = header_search_all(h, SC_RESOURCE_FIELD, &nb_resources);
	on_error goto q1;

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(win), vbox);

	// The header
	GtkWidget *title = gtk_label_new(NULL);
	char *title_str = g_markup_printf_escaped(
		"<b>From</b> <i>%s</i>, <b>Received on</b> <i>%s</i>\n"
		"<b>Subject :</b> %s",
		msg->from, ts2staticstr(msg->date), msg->descr);
	gtk_label_set_markup(GTK_LABEL(title), title_str);
	gtk_misc_set_alignment(GTK_MISC(title), 0, 0);
	gtk_label_set_line_wrap(GTK_LABEL(title), TRUE);
	gtk_label_set_line_wrap_mode(GTK_LABEL(title), PANGO_WRAP_WORD_CHAR);
	g_free(title_str);
	gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	// A Tab for each resources, except for the header which is put in an expander above
	for (unsigned n = 0; n < nb_resources; n++) {
		char resource[PATH_MAX];
		char title[64];
		char type[128];	// wild guess
		bool is_header = false;
		if_fail (header_stripped_value(resources[n], sizeof(resource), resource)) break;
		if_fail (header_copy_parameter("name", resources[n], sizeof(title), title)) {
			if (error_code() == ENOENT) {	// smtp header have no filename
				error_clear();
				is_header = true;
			} else break;
		}
		if_fail (header_copy_parameter("type", resources[n], sizeof(type), type)) {
			if (error_code() == ENOENT) {
				error_clear();
				type[0] = '\0';
			} else break;
		}
		GtkWidget *widget = make_view_widget(type, resource, GTK_WINDOW(win));
		if (is_header) {
			gtk_box_pack_start(GTK_BOX(vbox), make_expander("Headers", widget), FALSE, FALSE, 0);
		} else {
			if (title[0] == '\0') snprintf(title, sizeof(title), "Untitled");
			(void)gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget, gtk_label_new(title));
		}
	}
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	free(resources);

	GtkWidget *toolbar = make_toolbar(3,
		GTK_STOCK_JUMP_TO, NULL,     NULL,	// Forward
		GTK_STOCK_DELETE,  NULL,     NULL,	// Delete
		GTK_STOCK_QUIT,    close_cb, win);

#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(win), toolbar);
#	else
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
#	endif

q1:
	header_del(h);
	return win;
}
