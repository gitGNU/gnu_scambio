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

static void make_varbuf_from_resource(struct varbuf *vb, char const *resource)
{
	char filename[PATH_MAX];
	if_fail (chn_get_file(&ccnx, filename, resource)) {
		char *errstr = strdup(error_str());
		error_clear();	// replace by an error text
		if_succeed (varbuf_ctor(vb, 1024, true)) {
			varbuf_append_strs(vb, "Cannot get resource '", resource, "' : ", errstr, NULL);
			on_error varbuf_dtor(vb);
		}
		free(errstr);
		return;
	}
	wait_complete();
	if_fail (varbuf_ctor_from_file(vb, filename)) return;
}

static GtkWidget *make_framed_text_buffer(char const *title, struct varbuf *vb)
{
	GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), vb->buf, vb->used);
	GtkWidget *widget = gtk_text_view_new_with_buffer(buffer);
	g_object_unref(G_OBJECT(buffer));
	return make_frame(title, widget);
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
	unsigned nb_names;
	char const **names = header_search_all(h, SC_NAME_FIELD, &nb_names);
	on_error goto q2;

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

	// Look for a resource named "header"
	unsigned head_idx;
	for (head_idx = 0; head_idx < nb_names; head_idx++) {
		if (0 == strcmp(names[head_idx], "header")) break;
	}
	while (head_idx < nb_names && head_idx < nb_resources) {	// Try to add a header frame
		struct varbuf vb;
		if_fail (make_varbuf_from_resource(&vb, resources[head_idx])) break;
		gtk_container_add(GTK_CONTAINER(vbox), make_framed_text_buffer("Header", &vb));
		varbuf_dtor(&vb);
	}

	// For each other resources, add a Widget
	for (unsigned n = 0; n < nb_names; n++) {
		if (n == head_idx) continue;
		struct varbuf vb;
		if_fail (make_varbuf_from_resource(&vb, resources[n])) break;
		gtk_container_add(GTK_CONTAINER(vbox), make_framed_text_buffer(names[n], &vb));
		varbuf_dtor(&vb);
	}

//q3:
	free(names);
q2:
	free(resources);
q1:
	header_del(h);
	return win;
}
