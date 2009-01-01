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
#include "scambio.h"
#include "scambio/timetools.h"
#include "misc.h"
#include "vadrouille.h"

/*
 * Messages
 */

enum {
	FIELD_DESCR,
	FIELD_DATE,
	FIELD_NEW,
	FIELD_MSGPTR,
	NB_FIELDS
};

// Throws no error.
void sc_msg_ctor(struct sc_msg *msg, struct mdirb *mdirb, struct header *h, mdir_version version, struct sc_plugin *plugin)
{
	debug("msg@%p, version %"PRIversion, msg, version);
	msg->header = header_ref(h);
	msg->version = version;
	msg->mdirb = mdirb;
	msg->plugin = plugin;
	msg->was_read = false;	// untill proven otherwise
	msg->count = 1;
}

static struct sc_plugin default_plugin;

static struct sc_msg *msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct sc_msg *msg = Malloc(sizeof(*msg));
	sc_msg_ctor(msg, mdirb, h, version, &default_plugin);
	return msg;
}

void sc_msg_dtor(struct sc_msg *msg)
{
	debug("msg@%p", msg);
	assert(msg->count <= 0);
	header_unref(msg->header);
	msg->header = NULL;
}

static void msg_del(struct sc_msg *msg)
{
	sc_msg_dtor(msg);
	free(msg);
}

static char *msg_descr(struct sc_msg *msg)
{
	struct header *const h = msg->header;
	// Fetch everything we may need from the header
	struct header_field *type   = header_find(h, SC_TYPE_FIELD, NULL);
	struct header_field *name   = header_find(h, SC_NAME_FIELD, NULL);
	struct header_field *descr  = header_find(h, SC_DESCR_FIELD, NULL);
	struct header_field *from   = header_find(h, SC_FROM_FIELD, NULL);
	struct header_field *start  = header_find(h, SC_START_FIELD, NULL);
	struct header_field *stop   = header_find(h, SC_STOP_FIELD, NULL);
	//struct header_field *status = header_find(h, SC_STATUS_FIELD, NULL);
	
	char *ret = NULL;
	if (! type) {
		ret = g_markup_printf_escaped("<i>%s</i>%s%s",
			name ? name->value : "message",
			descr ? " : ":"",
			descr ? descr->value : " (?)");
	} else if (0 == strcmp(type->value, SC_DIR_TYPE)) {
		ret = g_markup_printf_escaped("Directory <b>%s</b>", name ? name->value : "unnamed !?");
	} else if (0 == strcmp(type->value, SC_MAIL_TYPE)) {
		ret = g_markup_printf_escaped("Mail from <b>%s</b> : %s",
			from ? from->value : "???",
			descr ? descr->value : "No subject");
	} else if (0 == strcmp(type->value, SC_CAL_TYPE)) {
		char start_str[32], stop_str[32];
		if (start) sc_gmfield2str(start_str, sizeof(start_str), start->value);
		if (stop)  sc_gmfield2str(stop_str,  sizeof(stop_str),  stop->value);
		unless_error {
			ret = g_markup_printf_escaped("%s%s%s : %s",
				start ? start_str : "",
				start && stop ? " - " : "",
				stop ? stop_str : "",
				descr ? descr->value : "No description");
		} else {
			ret = g_markup_printf_escaped("<b>%s</b>", error_str());
			error_clear();
		}
	} else if (0 == strcmp(type->value, SC_CONTACT_TYPE)) {
		ret = g_markup_printf_escaped("Contact <i>%s</i>", name ? name->value : "???");
	} else if (0 == strcmp(type->value, SC_BOOKMARK_TYPE)) {
		ret = g_markup_printf_escaped("Bookmark <i>%s</i>", name ? name->value : "???");
	}
	
	return ret;
}

extern inline struct sc_msg *sc_msg_ref(struct sc_msg *msg);
extern inline void sc_msg_unref(struct sc_msg *msg);

void sc_msg_mark_read(struct sc_msg *msg)
{
	if (! msg->was_read) {
		if_succeed (mdir_mark_read(&msg->mdirb->mdir, conf_get_str("SC_USERNAME"), msg->version)) {
			mdirb_refresh(msg->mdirb);
		}
		error_clear();
	}
}

/*
 * Generic Directory View
 */

extern inline void sc_dir_view_ctor(struct sc_dir_view *, struct sc_plugin *, struct mdirb *, GtkWidget *);
extern inline void sc_dir_view_dtor(struct sc_dir_view *);

struct gen_dir_view {
	struct sc_dir_view view;
	GtkListStore *store;
	GtkWidget *list;
};

static struct sc_msg *get_selected_msg(struct gen_dir_view *view)
{
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(view->list));
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view->list));
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		alert(GTK_MESSAGE_ERROR, "Select a message first");
		return NULL;
	}
	GValue gptr;
	memset(&gptr, 0, sizeof(gptr));
	gtk_tree_model_get_value(model, &iter, FIELD_MSGPTR, &gptr);
	struct sc_msg *msg = g_value_get_pointer(&gptr);
	g_value_unset(&gptr);
	return msg;
}

static void view_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct gen_dir_view *view = (struct gen_dir_view *)user_data;
	struct sc_msg *msg = get_selected_msg(view);
	if (! msg) return;
	if (msg->plugin->ops->msg_view_new) {
		(void)msg->plugin->ops->msg_view_new(msg);
	} else {
		alert(GTK_MESSAGE_ERROR, "Don't known how to view this message");
	}
}

static void del_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct gen_dir_view *view = (struct gen_dir_view *)user_data;
	struct sc_msg *msg = get_selected_msg(view);
	if (! msg) return;
	if (confirm("Delete this message ?")) {
		if_fail (mdir_del_request(&view->view.mdirb->mdir, msg->version)) {
			alert_error();
		}
		mdirb_refresh(view->view.mdirb);
	}
}

static void reload_store(struct gen_dir_view *view)
{
	gtk_list_store_clear(view->store);
	GtkTreeIter iter;
	struct sc_msg *msg;
	LIST_FOREACH (msg, &view->view.mdirb->msgs, entry) {
		char *descr = msg->plugin->ops->msg_descr(msg);
		char date[64] = "";
		struct header_field *hf = header_find(msg->header, SC_START_FIELD, NULL);
		if (hf) {
			if_fail (sc_gmfield2str(date, sizeof(date), hf->value)) {
				date[0] = '\0';
				error_clear();
			}
		}
		gtk_list_store_insert_with_values(view->store, &iter, G_MAXINT,
			FIELD_DESCR, descr,
			FIELD_DATE, date,
			FIELD_NEW, msg->was_read ? "":"New",
			FIELD_MSGPTR, msg,
			-1);
		g_free(descr);
	}
}

static void dir_view_ctor(struct gen_dir_view *view, struct mdirb *mdirb)
{
	GtkWidget *window = make_window(WC_MSGLIST, NULL, NULL);
	GtkWidget *page = gtk_vbox_new(FALSE, 0);
	
	view->store = gtk_list_store_new(NB_FIELDS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	view->list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(view->store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view->list), FALSE);
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

	gtk_tree_view_append_column(GTK_TREE_VIEW(view->list),
		gtk_tree_view_column_new_with_attributes("New", renderer,
			"text", FIELD_NEW,
			NULL));

	gtk_tree_view_append_column(GTK_TREE_VIEW(view->list),
		gtk_tree_view_column_new_with_attributes("Date", renderer,
			"text", FIELD_DATE,
			NULL));

	gtk_tree_view_append_column(GTK_TREE_VIEW(view->list),
		gtk_tree_view_column_new_with_attributes("Description", renderer,
			"markup", FIELD_DESCR,
			NULL));
	
	gtk_box_pack_start(GTK_BOX(page), make_scrollable(view->list), TRUE, TRUE, 0);
	GtkWidget *toolbar = make_toolbar(3,
		GTK_STOCK_OK,     view_cb,  view,	// View the selected message
		GTK_STOCK_DELETE, del_cb,   view,	// Delete the selected message
		GTK_STOCK_QUIT,   close_cb, window);
	gtk_box_pack_end(GTK_BOX(page), toolbar, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(window), page);
	sc_dir_view_ctor(&view->view, &default_plugin, mdirb, window);
	
	reload_store(view);
}

static struct sc_dir_view *dir_view_new(struct mdirb *mdirb)
{
	struct gen_dir_view *view = Malloc(sizeof(*view));
	if_fail (dir_view_ctor(view, mdirb)) {
		free(view);
		return NULL;
	}
	return &view->view;
}

static void dir_view_dtor(struct gen_dir_view *view)
{
	sc_dir_view_dtor(&view->view);
}

static void dir_view_del(struct sc_view *view_)
{
	struct gen_dir_view *view = DOWNCAST(view2dir_view(view_), view, gen_dir_view);
	dir_view_dtor(view);
	free(view);
}

static void dir_view_refresh(struct sc_dir_view *view)
{
	reload_store(DOWNCAST(view, view, gen_dir_view));
}

/*
 * Init
 */

static void function_list(struct mdirb *mdirb, char const *name, GtkWindow *parent)
{
	(void)parent;
	debug("listing message in mdirb@%p (%s)", mdirb, name);
	if_fail (dir_view_new(mdirb)) {
		alert(GTK_MESSAGE_ERROR, error_str());
		error_clear();
	}
}

static struct sc_plugin_ops const ops = {
	.msg_new          = msg_new,
	.msg_del          = msg_del,
	.msg_descr        = msg_descr,
	.msg_icon         = NULL,
	.msg_view_new     = NULL,
	.msg_view_del     = NULL,
	.dir_view_new     = dir_view_new,
	.dir_view_del     = dir_view_del,
	.dir_view_refresh = dir_view_refresh,
};
static struct sc_plugin default_plugin = {
	.name = "default",
	.type = NULL,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 1,
	.dir_functions = {
		{ NULL, "List", function_list },
	},
};

/*
 * Init
 */

void sc_msg_init(void)
{
	sc_plugin_register(&default_plugin);
}
