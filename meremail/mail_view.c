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
#include "merelib.h"
#include "meremail.h"
#include "varbuf.h"
#include "auth.h"
#include "scambio/channel.h"

/*
 * Data Definitions
 */

struct chn_cnx ccnx;

/*
 * Init
 */

void mail_view_init(void)
{
	if_fail (auth_begin()) return;
	atexit(auth_end);
	if_fail (chn_begin(false)) return;
	atexit(chn_end);
	// TODO: we could also put the filed host/port on the resource line, and use a pool of ccnx ?
	conf_set_default_str("SC_FILED_HOST", "localhost");
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
	conf_set_default_str("SC_USERNAME", "Alice");
	on_error return;
	if_fail (chn_cnx_ctor_outbound(&ccnx, conf_get_str("SC_FILED_HOST"), conf_get_str("SC_FILED_PORT"), conf_get_str("SC_USERNAME"))) return;
}

/*
 * Make a Window to display a mail
 */

static void wait_complete(void)
{
	debug("waiting...");
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
	while (! chn_cnx_all_tx_done(&ccnx)) pth_nanosleep(&ts, NULL);
}

static GtkWidget *make_framed_widget(char const *title, char const *type, char const *resource, bool expander)
{
	debug("title='%s', type='%s', resource='%s'", title, type, resource);
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
	wait_complete();
	// For text files, display in a text widget (after utf8 conversion)
	// For html, use GtkHtml,
	// For image, display as an image
	// For other types, display a launcher for external app
	if (0 == strncmp(type, "text/plain", 10)) {
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
		gchar *utf8text = g_convert_with_fallback(vb.buf, vb.used, "UTF-8", charset, NULL, NULL, &utf8size, NULL);
		varbuf_dtor(&vb);
		if (! utf8text) with_error(0, "Cannot convert from '%s' to utf8", charset) return NULL;
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), utf8text, utf8size);
		widget = gtk_text_view_new_with_buffer(buffer);
		g_object_unref(G_OBJECT(buffer));
		g_free(utf8text);
	} else if (0 == strncmp(type, "image", 5)) {
		widget = gtk_image_new_from_file(filename);	// Oh! All mighty gtk !
	}
	if (! widget) {
		widget = gtk_label_new(NULL);
		char *str = g_markup_printf_escaped("<b>Cannot display this part</b> which have type <i>%s</i>", type);
		gtk_label_set_markup(GTK_LABEL(widget), str);
		g_free(str);
	}
q:	return (expander ? make_expander : make_frame)(title, widget);
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

	GtkWidget *title = gtk_label_new(NULL);
	char *title_str = g_markup_printf_escaped(
		"<b>From</b> <i>%s</i>\n"
		"<b>Received on</b> <i>%s</i>\n"
		"%s",
		msg->from, ts2staticstr(msg->date), msg->descr);
	gtk_label_set_markup(GTK_LABEL(title), title_str);
	g_free(title_str);
	gtk_container_add(GTK_CONTAINER(vbox), title);

	// Add a frame for each resources
	for (unsigned n = 0; n < nb_resources; n++) {
		char resource[PATH_MAX];
		char title[64];
		char type[128];	// wild guess
		bool expander = false;
		if_fail (header_stripped_value(resources[n], sizeof(resource), resource)) break;
		if_fail (header_copy_parameter("name", resources[n], sizeof(title), title)) {
			if (error_code() == ENOENT) {	// smtp header have no filename
				error_clear();
				snprintf(title, sizeof(title), "Headers");
				expander = true;
			} else break;
		}
		if_fail (header_copy_parameter("type", resources[n], sizeof(type), type)) {
			if (error_code() == ENOENT) {
				error_clear();
				type[0] = '\0';
			} else break;
		}
		GtkWidget *widget = make_framed_widget(title, type, resource, expander);
		if (widget) gtk_container_add(GTK_CONTAINER(vbox), widget);
	}

	free(resources);
q1:
	header_del(h);
	return win;
}
