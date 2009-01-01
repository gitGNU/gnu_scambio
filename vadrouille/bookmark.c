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
#include "misc.h"
#include "merelib.h"
#include "vadrouille.h"
#include "dialog.h"
#include "bookmark.h"

/*
 * Bookmark Message are just plain sc_msg
 */

static struct sc_plugin plugin;

static struct sc_msg *bmark_msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct sc_msg *msg = Malloc(sizeof(*msg));
	if_fail (sc_msg_ctor(msg, mdirb, h, version, &plugin)) {
		free(msg);
		msg = NULL;
	}
	return msg;
}

static void bmark_msg_del(struct sc_msg *msg)
{
	sc_msg_dtor(msg);
	free(msg);
}

/*
 * View a Bookmark
 */

static char const *url_view_cmd_fmt;

static struct sc_msg_view *bookmark_view_new(struct sc_msg *msg)
{
	if (! url_view_cmd_fmt) {
		alert(GTK_MESSAGE_ERROR, "No URL viewer defined.");
		return NULL;
	}
	// We do not show the bookmark, but run an external web browser
	struct header_field *hf = header_find(msg->header, SC_URL_FIELD, NULL);
	if (! hf) {
		alert(GTK_MESSAGE_ERROR, "Empty bookmark ?");
		return NULL;
	}
	char cmd[PATH_MAX];	// Should be enough
	snprintf(cmd, sizeof(cmd), url_view_cmd_fmt, hf->value);
	if_fail (RunAsShell(cmd)) alert_error();
	return NULL;
}

/*
 * Directory Functions
 */

static void function_add_bmark(struct mdirb *mdirb, char const *name, GtkWindow *parent)
{
	(void)name;
	GtkWidget *name_entry  = gtk_entry_new();
	GtkWidget *url_entry   = gtk_entry_new();
	GtkWidget *descr_entry = gtk_entry_new();
	struct sc_dialog *dialog = sc_dialog_new("New BookMark", parent,
		make_labeled_hboxes(3,
			"Name :", name_entry,
			"URL :", url_entry,
			"Description :", descr_entry));
	if (sc_dialog_accept(dialog)) {
		char const *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
		char const *url = gtk_entry_get_text(GTK_ENTRY(url_entry));
		char const *descr = gtk_entry_get_text(GTK_ENTRY(descr_entry));
		debug("New bookmark with name '%s' -> '%s'", name, url);
		struct header *h = header_new();
		(void)header_field_new(h, SC_TYPE_FIELD, SC_BOOKMARK_TYPE);
		(void)header_field_new(h, SC_NAME_FIELD, name);
		(void)header_field_new(h, SC_URL_FIELD, url);
		if (strlen(descr) > 0) {
			(void)header_field_new(h, SC_DESCR_FIELD, descr);
		}
		mdir_patch_request(&mdirb->mdir, MDIR_ADD, h);
		header_unref(h);
		on_error {
			alert_error();
		} else {
			mdirb_refresh(mdirb);
		}
	}
	sc_dialog_del(dialog);
}

/*
 * Message Description
 */

static char *bmark_msg_descr(struct sc_msg *msg)
{
	char const *name = "";
	struct header_field *hf = header_find(msg->header, SC_NAME_FIELD, NULL);
	if (hf) name = hf->value;

	char const *url = "Not a Bookmark";	// FIXME: we may want to bookmark resources also
	hf = header_find(msg->header, SC_URL_FIELD, NULL);
	if (hf) url = hf->value;

	hf = header_find(msg->header, SC_DESCR_FIELD, NULL);

	return g_markup_printf_escaped("<b>%s</b> (%s)%s%s",
		name, url, hf ? "\n":"", hf ? hf->value : "");
}

static char *bmark_msg_icon(struct sc_msg *msg)
{
	(void)msg;
	return GTK_STOCK_INDEX;
}

/*
 * Init
 */

static struct sc_plugin_ops const ops = {
	.msg_new          = bmark_msg_new,
	.msg_del          = bmark_msg_del,
	.msg_descr        = bmark_msg_descr,
	.msg_icon         = bmark_msg_icon,
	.msg_view_new     = bookmark_view_new,
	.msg_view_del     = NULL,
	.dir_view_new     = NULL,
	.dir_view_del     = NULL,
	.dir_view_refresh = NULL,
};
static struct sc_plugin plugin = {
	.name = "bookmark",
	.type = SC_BOOKMARK_TYPE,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 1,
	.dir_functions = {
		{ NULL, "Add", function_add_bmark },
	},
};

void bookmark_init(void)
{
	struct header_field *hf = header_find(mdir_user_header(user), "url-viewer", NULL);
	if (hf) {
		url_view_cmd_fmt = hf->value;
	} else {
		warning("No command defined to view URLs");
	}
	sc_plugin_register(&plugin);
}
