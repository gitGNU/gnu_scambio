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
#include "scambio/channel.h"
#include "scambio/timetools.h"
#include "mail.h"
#include "varbuf.h"
#include "misc.h"
#include "vadrouille.h"

/*
 * Mail Message
 */

struct mail_msg {
	struct sc_msg msg;
	char *from;
	char *descr;
	time_t date;
};

static inline struct mail_msg *msg2mmsg(struct sc_msg *msg)
{
	return DOWNCAST(msg, msg, mail_msg);
}

static struct sc_plugin plugin;
static void mail_msg_ctor(struct mail_msg *mmsg, struct mdirb *mdirb, struct header *h, mdir_version version)
{
	debug("msg version %"PRIversion, version);

	// To be a mail message, a new patch must have a from, descr and start field (duck typing)
	struct header_field *from = header_find(h, SC_FROM_FIELD, NULL);
	struct header_field *descr = header_find(h, SC_DESCR_FIELD, NULL);
	struct header_field *date = header_find(h, SC_START_FIELD, NULL);
	if (!from || !descr || !date) with_error(0, "Not a mail") return;

	bool dummy;
	if_fail (mmsg->date = sc_gmfield2ts(date->value, &dummy)) return;
	mmsg->from = Strdup(from->value);
	mmsg->descr = Strdup(descr->value);

	sc_msg_ctor(&mmsg->msg, mdirb, h, version, &plugin);
}

static struct sc_msg *mail_msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct mail_msg *mmsg = Malloc(sizeof(*mmsg));
	if_fail (mail_msg_ctor(mmsg, mdirb, h, version)) {
		free(mmsg);
		return NULL;
	}
	return &mmsg->msg;
}

static void mail_msg_dtor(struct mail_msg *mmsg)
{
	debug("mmsg '%s'", mmsg->descr);
	FreeIfSet(&mmsg->from);
	FreeIfSet(&mmsg->descr);
	sc_msg_dtor(&mmsg->msg);
}

static void mail_msg_del(struct sc_msg *msg)
{
	struct mail_msg *mmsg = msg2mmsg(msg);
	mail_msg_dtor(mmsg);
	free(mmsg);
}

static char *mail_msg_descr(struct sc_msg *msg)
{
	struct mail_msg *mmsg = msg2mmsg(msg);
	return g_markup_printf_escaped("Mail from <b>%s</b> : %s", mmsg->from, mmsg->descr);
}

/*
 * Message view
 */

struct mail_msg_view {
	struct sc_msg_view view;
};

// type is allowed to be NULL
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
	if (type && 0 == strncmp(type, "text/html", 9)) {
		if_fail (varbuf_ctor_from_file(&vb, filename)) return NULL;
		widget = gtk_html_new_from_string(vb.buf, vb.used);
		varbuf_dtor(&vb);
	} else if (type && 0 == strncmp(type, "text/", 5)) {	// all other text types are treated the same
		GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
		char *charset = parameter_extract(type, "charset");
		if (! charset) charset = Strdup("us-ascii");
		do {
			if_fail (varbuf_ctor_from_file(&vb, filename)) break;
			// convert to UTF-8
			gsize utf8size;
			gchar *utf8text = g_convert_with_fallback(vb.buf, vb.used, "utf-8", charset, NULL, NULL, &utf8size, NULL);
			varbuf_dtor(&vb);
			if (! utf8text) with_error(0, "Cannot convert from '%s' to utf8", charset) break;
			gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), utf8text, utf8size);
			widget = gtk_text_view_new_with_buffer(buffer);
			g_object_unref(G_OBJECT(buffer));
			g_free(utf8text);
		} while (0);
		free(charset);
		on_error return NULL;
	} else if (type && 0 == strncmp(type, "image/", 6)) {
		widget = gtk_image_new_from_file(filename);	// Oh! All mighty gtk !
	}
	if (! widget) {
		widget = gtk_label_new(NULL);
		char *str = g_markup_printf_escaped("<b>Cannot display this part</b> which have type '<i>%s</i>'", type ? type:"NONE");
		gtk_label_set_markup(GTK_LABEL(widget), str);
		g_free(str);
	}
q:	return make_scrollable(widget);
}

static char const *ts2staticstr(time_t ts)
{
	static char date_str[64];
	struct tm *tm = localtime(&ts);
	(void)strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm);
	return date_str;
}

static void delete_cb(GtkToolButton *button, gpointer *user_data)
{
	(void)button;
	struct mail_msg_view *view = (struct mail_msg_view *)user_data;
	debug("msg version = %"PRIversion, view->view.msg->version);
	if (confirm("Are you sure, etc...?")) {
		if_succeed (mdir_del_request(&view->view.msg->mdirb->mdir, view->view.msg->version)) {
			mdirb_refresh(view->view.msg->mdirb);
			gtk_widget_destroy(view->view.view.window);
		} else {
			alert_error();
		}
	}
}

static void mail_msg_view_ctor(struct mail_msg_view *view, struct sc_msg *msg)
{
	/* The mail view is merely a succession of all the attached files,
	 * preceeded with our mail patch.
	 */
	debug("view=%p, msg=%p", view, msg);
	struct mail_msg *mmsg = msg2mmsg(msg);

	GtkWidget *window = make_window(WC_VIEWER, NULL, NULL);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	// The header
	GtkWidget *title = gtk_label_new(NULL);
	char *title_str = g_markup_printf_escaped(
		"<b>From</b> <i>%s</i>, <b>Received on</b> <i>%s</i>\n"
		"<b>Subject :</b> %s",
		mmsg->from, ts2staticstr(mmsg->date), mmsg->descr);
	gtk_label_set_markup(GTK_LABEL(title), title_str);
	g_free(title_str);
	gtk_misc_set_alignment(GTK_MISC(title), 0, 0);
	gtk_label_set_line_wrap(GTK_LABEL(title), TRUE);
#	if TRUE == GTK_CHECK_VERSION(2, 10, 0)
	gtk_label_set_line_wrap_mode(GTK_LABEL(title), PANGO_WRAP_WORD_CHAR);
#	endif
	gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
	// A Tab for each resource, except for the header which is put in an expander above
	struct header_field *resource = NULL;
	while (NULL != (resource = header_find(msg->header, SC_RESOURCE_FIELD, resource))) {
		char *resource_stripped = parameter_suppress(resource->value);
		char *title = parameter_extract(resource->value, "name");
		bool is_header = !title;
		char *type = parameter_extract(resource->value, "type");
		GtkWidget *widget = make_view_widget(type, resource_stripped, GTK_WINDOW(window));
		if (is_header) {
			gtk_box_pack_start(GTK_BOX(vbox), make_expander("Headers", widget), FALSE, FALSE, 0);
		} else {
			(void)gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widget, gtk_label_new(title ? title:"Untitled"));
		}
		FreeIfSet(&title);
		FreeIfSet(&type);
		FreeIfSet(&resource_stripped);
	}
	gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

	GtkWidget *toolbar = make_toolbar(3,
		GTK_STOCK_JUMP_TO, NULL,      NULL,	// Forward
		GTK_STOCK_DELETE,  delete_cb, view,	// Delete
		GTK_STOCK_QUIT,    close_cb,  window);

	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);
	
	sc_msg_view_ctor(&view->view, &plugin, msg, window);
}

static struct sc_msg_view *mail_msg_view_new(struct sc_msg *msg)
{
	struct mail_msg_view *view = Malloc(sizeof(*view));
	if_fail (mail_msg_view_ctor(view, msg)) {
		free(view);
		return NULL;
	}
	return &view->view;
}

static void mail_msg_view_dtor(struct mail_msg_view *view)
{
	sc_msg_view_dtor(&view->view);
}

static void mail_msg_view_del(struct sc_view *view)
{
	struct mail_msg_view *mview = DOWNCAST(view2msg_view(view), view, mail_msg_view);
	mail_msg_view_dtor(mview);
	free(mview);
}

/*
 * Additional Functions
 */

static void compose_cb(GtkWindow *parent)
{
	(void)parent;
	if_fail ((void)mail_composer_new(NULL, NULL, NULL)) alert_error();
}

/*
 * Init
 */

struct mdir *mail_outbox;

static struct sc_plugin_ops const ops = {
	.msg_new          = mail_msg_new,
	.msg_del          = mail_msg_del,
	.msg_descr        = mail_msg_descr,
	.msg_view_new     = mail_msg_view_new,
	.msg_view_del     = mail_msg_view_del,
	.dir_view_new     = NULL,
	.dir_view_del     = NULL,
	.dir_view_refresh = NULL,
};
static struct sc_plugin plugin = {
	.name = "mail",
	.type = SC_MAIL_TYPE,
	.ops = &ops,
	.nb_global_functions = 1,
	.global_functions = {
		{ NULL, "New", compose_cb },
	},
	.nb_dir_functions = 0,
	.dir_functions = {},
};

void mail_init(void)
{
	struct header_field *outbox_name = header_find(mdir_user_header(user), "smtp-outbox", NULL);
	if (outbox_name) {
		if_fail (mail_outbox = mdir_lookup(outbox_name->value)) return;
	} else {
		warning("No outbox defined : Cannot send");
	}
	sc_plugin_register(&plugin);
}

