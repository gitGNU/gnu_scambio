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

static struct sc_plugin bmark_plugin;

struct bmark_msg {
	struct sc_msg msg;
	char const *name, *url, *descr;	// points onto msg.header
};

static inline struct bmark_msg *msg2bmark(struct sc_msg *msg)
{
	return DOWNCAST(msg, msg, bmark_msg);
}

static void bmark_msg_ctor(struct bmark_msg *msg, struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct header_field *name_field = header_find(h, SC_NAME_FIELD, NULL);
	struct header_field *descr_field = header_find(h, SC_DESCR_FIELD, NULL);
	struct header_field *url_field = header_find(h, SC_URL_FIELD, NULL);
	if (!name_field || !url_field) with_error(0, "Not a bookmark") return;
	msg->name = name_field->value;
	msg->url = url_field->value;
	msg->descr = descr_field ? descr_field->value : NULL;
	sc_msg_ctor(&msg->msg, mdirb, h, version, &bmark_plugin);
}

static struct sc_msg *bmark_msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct bmark_msg *msg = Malloc(sizeof(*msg));
	if_fail (bmark_msg_ctor(msg, mdirb, h, version)) {
		free(msg);
		return NULL;
	}
	return &msg->msg;
}

static void bmark_msg_dtor(struct bmark_msg *msg)
{
	sc_msg_dtor(&msg->msg);
}

static void bmark_msg_del(struct sc_msg *msg_)
{
	struct bmark_msg *msg = msg2bmark(msg_);
	bmark_msg_dtor(msg);
	free(msg);
}

/*
 * View a Bookmark
 */

static char const *url_view_cmd_fmt;

static struct sc_msg_view *bookmark_view_new(struct sc_msg *msg_)
{
	struct bmark_msg *msg = msg2bmark(msg_);

	if (! url_view_cmd_fmt) {
		alert(GTK_MESSAGE_ERROR, "No URL viewer defined.");
		return NULL;
	}
	
	// We do not show the bookmark, but run an external web browser
	char cmd[PATH_MAX];	// Should be enough
	snprintf(cmd, sizeof(cmd), url_view_cmd_fmt, msg->url);
	if_fail (RunAsShell(cmd)) alert_error();

	// Mark the bookmark as visited
	sc_msg_mark_read(&msg->msg);

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
		(void)header_field_new(h, SC_HAVE_READ_FIELD, mdir_user_name(user));
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

static char *bmark_msg_descr(struct sc_msg *msg_)
{
	struct bmark_msg *msg = msg2bmark(msg_);

	return g_markup_printf_escaped("<b>%s</b> (%s)%s%s",
		msg->name, msg->url, msg->descr ? "\n":"", msg->descr ? msg->descr : "");
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
static struct sc_plugin bmark_plugin = {
	.name = "bookmark",
	.type = SC_BOOKMARK_TYPE,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 1,
	.dir_functions = {
		{ TOSTR(ICONDIR)"/26x26/apps/bmark.png", NULL, function_add_bmark },
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
	sc_plugin_register(&bmark_plugin);
}
