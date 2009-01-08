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
#include <assert.h>
#include "misc.h"
#include "merelib.h"
#include "vadrouille.h"
#include "browser.h"
#include "dialog.h"

enum {
	FIELD_NAME,
	FIELD_SIZE,
	FIELD_MDIR,
	NB_FIELDS
};

enum {
	NEWS_FIELD_ICON,
	NEWS_FIELD_DESCR,
	NEWS_FIELD_MSG,
	NB_NEWS_FIELDS
};

/*
 * Construction
 */

// The window was destroyed for some reason : release the ref to it
static void unref_win(GtkWidget *widget, gpointer data)
{
	debug("unref browser window");
	(void)widget;
	struct browser *const browser = (struct browser *)data;
	if (browser->window) browser->window = NULL;
	browser_del(browser);
}

static struct mdirb *get_selected_mdirb(struct browser *browser, size_t name_size, char *name, struct mdirb **parent)
{
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree));
	GtkTreeIter iter;
	if (TRUE != gtk_tree_selection_get_selected(selection, NULL, &iter)) {
		debug("No selection");
		return NULL;
	}
	GValue gptr, gname;
	memset(&gptr, 0, sizeof(gptr));
	memset(&gname, 0, sizeof(gname));
	gtk_tree_model_get_value(GTK_TREE_MODEL(browser->store), &iter, FIELD_MDIR, &gptr);
	gtk_tree_model_get_value(GTK_TREE_MODEL(browser->store), &iter, FIELD_NAME, &gname);
	assert(G_VALUE_HOLDS_POINTER(&gptr));
	assert(G_VALUE_HOLDS_STRING(&gname));
	struct mdirb *mdirb = g_value_get_pointer(&gptr);
	if (name) snprintf(name, name_size, "%s", g_value_get_string(&gname));
	g_value_unset(&gptr);
	g_value_unset(&gname);
	if (parent) {	// we want the node before this one on the tree
		GtkTreeIter parent_iter;
		if (TRUE == gtk_tree_model_iter_parent(GTK_TREE_MODEL(browser->store), &parent_iter, &iter)) {
			memset(&gptr, 0, sizeof(gptr));
			gtk_tree_model_get_value(GTK_TREE_MODEL(browser->store), &parent_iter, FIELD_MDIR, &gptr);
			assert(G_VALUE_HOLDS_POINTER(&gptr));
			*parent = g_value_get_pointer(&gptr);
			g_value_unset(&gptr);
		} else {
			*parent = NULL;
		}
	}
	return mdirb;
}

static void dir_function_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct dirfunc2myself *d2m = (struct dirfunc2myself *)user_data;
	struct browser *browser = d2m->myself;
	
	// Retrieve select row, check there is something in there, and change the view
	char name[PATH_MAX];
	struct mdirb *mdirb = get_selected_mdirb(browser, sizeof(name), name, NULL);
	if (! mdirb) {
		alert(GTK_MESSAGE_ERROR, "Select a folder first");
		return;
	}
	debug("Execute dir function for '%s'", mdirb->mdir.path);
	if_fail (d2m->function->cb(mdirb, name, GTK_WINDOW(browser->window))) alert_error();
}

static void global_function_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct globfunc2myself *g2m = (struct globfunc2myself *)user_data;
	struct browser *browser = g2m->myself;

	debug("Execute global function");
	if_fail (g2m->function->cb(GTK_WINDOW(browser->window))) alert_error();
}

static void refresh_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct browser *browser = (struct browser *)user_data;
	browser_refresh(browser);
}

static void deldir_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct browser *browser = (struct browser *)user_data;
	char name[PATH_MAX];
	struct mdirb *parent;
	struct mdirb *mdirb = get_selected_mdirb(browser, sizeof(name), name, &parent);
	if (! mdirb) {
		alert(GTK_MESSAGE_ERROR, "Select first a folder as parent");
		return;
	}
	if (! parent) {
		alert(GTK_MESSAGE_ERROR, "You must not delete the root folder");
		return;
	}
	mdir_version to_del = mdir_get_folder_version(&parent->mdir, name);
	on_error {
		alert_error();
		return;
	}
	debug("Asked to delete folder named '%s' in parent dir '%s', version was %"PRIversion, name, parent->mdir.path, to_del);
	if (confirm("Aren't you afraid to remove this folder ?")) {
		// To unmount a folder, one must delete the patch that mounted it
		mdir_del_request(&parent->mdir, to_del, user);
		browser_refresh(browser);
	}
}

static void rename_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct browser *browser = (struct browser *)user_data;
	char old_name[PATH_MAX];
	struct mdirb *parent;
	struct mdirb *mdirb = get_selected_mdirb(browser, sizeof(old_name), old_name, &parent);
	if (! mdirb) {
		alert(GTK_MESSAGE_ERROR, "Rename what folder ?");
		return;
	}
	mdir_version old_version = mdir_get_folder_version(&parent->mdir, old_name);
	on_error {
		alert_error();
		return;
	}
	struct header *old_mount_point = mdir_read(&parent->mdir, old_version, NULL);
	on_error {
		alert_error();
		return;
	}
	struct header_field *dirId = header_find(old_mount_point, SC_DIRID_FIELD, NULL);
	if (! dirId) {
		alert(GTK_MESSAGE_ERROR, "Cannot find dirId of this mount point !");
		return;
	}
	GtkWidget *name_entry = gtk_entry_new();
	struct sc_dialog *dialog = sc_dialog_new("Rename folder",
		GTK_WINDOW(browser->window),
		make_labeled_hbox("New name", name_entry));
	if (sc_dialog_accept(dialog)) {
		debug("Rename folder '%s' to name : '%s'", old_name, gtk_entry_get_text(GTK_ENTRY(name_entry)));
		// Create first the new mount point
		struct header *h = header_new();
		(void)header_field_new(h, SC_TYPE_FIELD, SC_DIR_TYPE);
		(void)header_field_new(h, SC_NAME_FIELD, gtk_entry_get_text(GTK_ENTRY(name_entry)));
		(void)header_field_new(h, SC_DIRID_FIELD, dirId->value);
		mdir_patch_request(&parent->mdir, MDIR_ADD, h, user);
		header_unref(h);
		on_error {
			alert_error();
		} else {
			// Remove the former one
			mdir_del_request(&parent->mdir, old_version, user);
			browser_refresh(browser);
		}
	}
	sc_dialog_del(dialog);
	header_unref(old_mount_point);
}

static void newdir_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	struct browser *browser = (struct browser *)user_data;
	struct mdirb *mdirb = get_selected_mdirb(browser, 0, NULL, NULL);
	if (! mdirb) {
		alert(GTK_MESSAGE_ERROR, "Select first a folder as parent");
		return;
	}
	GtkWidget *name_entry = gtk_entry_new();
	struct sc_dialog *dialog = sc_dialog_new("New folder",
		GTK_WINDOW(browser->window),
		make_labeled_hbox("Name", name_entry));
	if (sc_dialog_accept(dialog)) {
		debug("New folder with name : '%s'", gtk_entry_get_text(GTK_ENTRY(name_entry)));
		// Build the patch
		struct header *h = header_new();
		(void)header_field_new(h, SC_TYPE_FIELD, SC_DIR_TYPE);
		(void)header_field_new(h, SC_NAME_FIELD, gtk_entry_get_text(GTK_ENTRY(name_entry)));
		mdir_patch_request(&mdirb->mdir, MDIR_ADD, h, user);
		header_unref(h);
		on_error {
			alert_error();
		} else {
			browser_refresh(browser);
		}
	}
	sc_dialog_del(dialog);
}

struct look4notif_param {
	struct sc_msg *msg;
	GtkTreeIter iter;
	bool found;
};

static gboolean look4notif(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	(void)path;
	struct look4notif_param *param = (struct look4notif_param *)data;
	GValue gptr;
	memset(&gptr, 0, sizeof(gptr));
	gtk_tree_model_get_value(model, iter, NEWS_FIELD_MSG, &gptr);
	assert(G_VALUE_HOLDS_POINTER(&gptr));
	struct sc_msg *msg = g_value_get_pointer(&gptr);
	g_value_unset(&gptr);
	if (msg == param->msg) {
		param->iter = *iter;
		param->found = true;
		return TRUE;
	}
	return FALSE;
}

static void new_message_notif(struct sc_msg_listener *listener, struct mdirb *mdirb, enum mdir_action action, struct sc_msg *msg)
{
	(void)mdirb;
	struct browser *browser = DOWNCAST(listener, msg_listener, browser);

	if (action == MDIR_ADD) {
		// We want new stuff only
		if (msg->was_read) return;

		char *descr = msg->plugin->ops->msg_descr(msg);
		char *icon = msg->plugin->ops->msg_icon ? msg->plugin->ops->msg_icon(msg) : NULL;

		// Prepend the new message
		GtkTreeIter iter;
		gtk_list_store_prepend(browser->news_list, &iter);
		gtk_list_store_set(browser->news_list, &iter,
			NEWS_FIELD_ICON, icon ? icon : GTK_STOCK_CANCEL,
			NEWS_FIELD_MSG, msg,
			NEWS_FIELD_DESCR, descr,
			-1);
		g_free(descr);

	} else {
		assert(action == MDIR_REM);

		struct look4notif_param param = {
			.found = false,
			.msg = msg,
		};
		gtk_tree_model_foreach(GTK_TREE_MODEL(browser->news_list), look4notif, &param);
		if (param.found) {
			gtk_list_store_remove(browser->news_list, &param.iter);
		}
	}
}

static void browser_ctor(struct browser *browser, char const *root)
{
	debug("brower@%p", browser);

	struct mdir *mdir = mdir_lookup(root);
	on_error return;
	browser->mdirb = mdir2mdirb(mdir);
	browser->iter = NULL;
	browser->nb_d2m = 0;
	browser->nb_g2m = 0;

	browser->store = gtk_tree_store_new(NB_FIELDS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	browser->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser->store));
#	if TRUE == GTK_CHECK_VERSION(2, 10, 0)
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(browser->tree), TRUE);
#	endif
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(browser->tree), FALSE);

	GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
		"Name",
		text_renderer,
		"text", FIELD_NAME,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(browser->tree), column);

	column = gtk_tree_view_column_new_with_attributes(
		"Size",
		text_renderer,
		"markup", FIELD_SIZE,
		NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(browser->tree), column);

	browser->window = make_window(WC_FOLDERS, unref_win, browser);

	GtkWidget *toolbar = make_toolbar(5,
		GTK_STOCK_DELETE,  deldir_cb,  browser,
		GTK_STOCK_REFRESH, refresh_cb, browser,
		GTK_STOCK_NEW,     newdir_cb,  browser,
		GTK_STOCK_CONVERT, rename_cb,  browser,
		GTK_STOCK_QUIT,    close_cb,   browser->window);
	// Add per plugin global functions
	struct sc_plugin *plugin;
	LIST_FOREACH(plugin, &sc_plugins, entry) {
		if (plugin->nb_global_functions == 0 && plugin->nb_dir_functions == 0) continue;
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), gtk_separator_tool_item_new(), -1);
		GtkToolItem *title = gtk_tool_item_new();
		gtk_container_add(GTK_CONTAINER(title), gtk_label_new(plugin->name));
		gtk_toolbar_insert(GTK_TOOLBAR(toolbar), title, -1);
		for (unsigned f = 0; f < plugin->nb_global_functions; f++) {
			GtkToolItem *button = gtk_tool_button_new(plugin->global_functions[f].icon, plugin->global_functions[f].label);
			gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button, -1);
			COMPILE_ASSERT(sizeof_array(browser->globfunc2myself) >= sizeof_array(plugin->global_functions));
			browser->globfunc2myself[browser->nb_g2m].function = plugin->global_functions+f;
			browser->globfunc2myself[browser->nb_g2m].myself = browser;
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(global_function_cb), browser->globfunc2myself+browser->nb_g2m);
			browser->nb_g2m++;
		}
		for (unsigned f = 0; f < plugin->nb_dir_functions; f++) {
			GtkToolItem *button = gtk_tool_button_new(plugin->dir_functions[f].icon, plugin->dir_functions[f].label);
			gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button, -1);
			COMPILE_ASSERT(sizeof_array(browser->dirfunc2myself) >= sizeof_array(plugin->dir_functions));
			browser->dirfunc2myself[browser->nb_d2m].function = plugin->dir_functions+f;
			browser->dirfunc2myself[browser->nb_d2m].myself = browser;
			g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(dir_function_cb), browser->dirfunc2myself+browser->nb_d2m);
			browser->nb_d2m++;
		}
	}

	// The news panel
	browser->news_list = gtk_list_store_new(NB_NEWS_FIELDS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	browser->news = gtk_tree_view_new_with_model(GTK_TREE_MODEL(browser->news_list));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(browser->news), FALSE);
	GtkCellRenderer *icon_renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(browser->news),
		gtk_tree_view_column_new_with_attributes("Icon", icon_renderer,
			"stock-id", NEWS_FIELD_ICON,
			NULL));
	gtk_tree_view_append_column(GTK_TREE_VIEW(browser->news),
		gtk_tree_view_column_new_with_attributes("Message", text_renderer,
			"markup", NEWS_FIELD_DESCR,
			NULL));

	GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
	GtkWidget *paned = gtk_vpaned_new();
	gtk_paned_pack1(GTK_PANED(paned), make_scrollable(browser->tree), TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(paned), make_scrollable(browser->news), FALSE, TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(browser->window), vbox);

	browser_refresh(browser);
	sc_msg_listener_ctor(&browser->msg_listener, new_message_notif);

	gtk_widget_show_all(browser->window);
}

struct browser *browser_new(char const *root)
{
	struct browser *browser = Malloc(sizeof(*browser));
	if_fail (browser_ctor(browser, root)) {
		free(browser);
		browser = NULL;
	}
	return browser;
}

/*
 * Destruction
 */

static void browser_dtor(struct browser *browser)
{
	debug("brower@%p", browser);
	if (browser->window) {	// may not be present
		gtk_widget_destroy(browser->window);
		browser->window = NULL;
	}
	sc_msg_listener_dtor(&browser->msg_listener);
}

void browser_del(struct browser *browser)
{
	browser_dtor(browser);
	free(browser);
}

/*
 * Refresh
 */

static void add_subfolder_rec(struct browser *browser, char const *name);
static void add_subfolder_cb(struct mdir *parent, struct mdir *child, bool synched, char const *name, void *data)
{
	(void)parent;
	(void)synched;	// TODO: display this in a way or another (italic ?)
	struct browser *browser = (struct browser *)data;

	// Recursive call
	struct mdirb *prev_mdirb = browser->mdirb;
	browser->mdirb = mdir2mdirb(child);
	add_subfolder_rec(browser, name);
	browser->mdirb = prev_mdirb;
}

static void add_subfolder_rec(struct browser *browser, char const *name)
{
	// Add this name as a child of the given iterator
	mdirb_refresh(browser->mdirb);
	debug("mdir '%s' (%s) has size %u", name, browser->mdirb->mdir.path, browser->mdirb->nb_msgs);
	GtkTreeIter iter;
	gtk_tree_store_append(browser->store, &iter, browser->iter);
	char *size_str;
	if (browser->mdirb->nb_unread > 0) {
		size_str = g_markup_printf_escaped("<b>%u</b>/%u", browser->mdirb->nb_unread, browser->mdirb->nb_msgs);
	} else if (browser->mdirb->nb_msgs > 0) {
		size_str = g_markup_printf_escaped("%u", browser->mdirb->nb_msgs);
	} else {
		size_str = g_markup_printf_escaped("<small>-</small>");
	}
	gtk_tree_store_set(browser->store, &iter,
		FIELD_NAME, name,
		FIELD_SIZE, size_str,
		FIELD_MDIR, browser->mdirb,
		-1);
	g_free(size_str);

	if (browser->previously_selected == browser->mdirb) {
		// We cannot merely select the iter her, since addition of new rows will
		// empty the selection (!)
		// So we save the iterator for later
		browser->selected_iter = iter;
		browser->selected_iter_set = true;
	}

	// Jump into this new iter
	GtkTreeIter *prev_iter = browser->iter;
	browser->iter = &iter;
	// and find all the children
	mdir_folder_list(&browser->mdirb->mdir, false, add_subfolder_cb, browser);
	browser->iter = prev_iter;
}

void browser_refresh(struct browser *browser)
{
	browser->previously_selected = get_selected_mdirb(browser, 0, NULL, NULL);
	browser->selected_iter_set = false;
	gtk_tree_store_clear(browser->store);
	add_subfolder_rec(browser, "root");
	gtk_tree_view_expand_all(GTK_TREE_VIEW(browser->tree));
	if (browser->selected_iter_set) {
		debug("Setting selection back to previous");
		GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(browser->tree));
		gtk_tree_selection_select_iter(selection, &browser->selected_iter);
	}
}

/*
 * Init
 */

void browser_init(void)
{
}


