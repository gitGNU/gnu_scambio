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
#include "scambio.h"
#include "misc.h"
#include "vadrouille.h"
#include "merelib.h"
#include "contact.h"
#include "dialog.h"

/*
 * All contacts are chained together to be used by the contact picker
 */

static LIST_HEAD(contacts, contact) contacts = LIST_HEAD_INITIALIZER(&contacts);

/*
 * Contact Message
 */

static inline struct contact *msg2contact(struct sc_msg *msg)
{
	return DOWNCAST(msg, msg, contact);
}

static struct sc_plugin plugin;
static void contact_ctor(struct contact *ct, struct mdirb *mdirb, struct header *h, mdir_version version)
{
	debug("contact version %"PRIversion, version);

	// To be a contact, a new patch must have a name
	struct header_field *name = header_find(h, SC_NAME_FIELD, NULL);
	if (! name) with_error(0, "Not a contact") return;
	ct->name = name->value;

	sc_msg_ctor(&ct->msg, mdirb, h, version, &plugin);
	LIST_INSERT_HEAD(&contacts, ct, entry);
}

static struct sc_msg *contact_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct contact *ct = Malloc(sizeof(*ct));
	if_fail (contact_ctor(ct, mdirb, h, version)) {
		free(ct);
		return NULL;
	}
	return &ct->msg;
}

static void contact_dtor(struct contact *ct)
{
	debug("contact '%s'", ct->name);
	LIST_REMOVE(ct, entry);
	sc_msg_dtor(&ct->msg);
}

static void contact_del(struct sc_msg *msg)
{
	struct contact *contact = msg2contact(msg);
	contact_dtor(contact);
	free(contact);
}

static char *contact_descr(struct sc_msg *msg)
{
	struct contact *ct = msg2contact(msg);
	return g_markup_printf_escaped("%s", ct->name);
}

static char *contact_icon(struct sc_msg *msg)
{
	(void)msg;
	return GTK_STOCK_ORIENTATION_PORTRAIT;
}

/*
 * Contact View :
 *
 * - a header with the id picture and the name (sc-picture, sc-name)
 * - then a box per category (category is the first-part of a field name, like work or home or whatever - sc being for internal use)
 * Resources are not shown until the user clicks on them (download only the picture at first).
 */

struct contact_view {
	LIST_HEAD(categories, category) categories;
	struct sc_msg_view view;
};

struct category {
	LIST_ENTRY(category) entry;
	char *name;
	struct contact_view *ctv;	// backlink
	unsigned nb_values;
	struct cat_value {
		char *value;	// allocated, stripped version of hf->value
		struct header_field *hf;	// in the original contact header
		struct category *cat;	// backlink for ease of use from callbacks
	} values[32];
};

// Throw no error.
static void category_ctor(struct category *cat, struct contact_view *ctv, char const *name)
{
	cat->nb_values = 0;
	cat->name = Strdup(name);
	cat->ctv = ctv;
	LIST_INSERT_HEAD(&ctv->categories, cat, entry);
}

// Throw no error.
static struct category *category_new(struct contact_view *ctv, char const *name)
{
	struct category *cat = Malloc(sizeof(*cat));
	category_ctor(cat, ctv, name);
	return cat;
}

static void category_add_value(struct category *cat, struct header_field *hf)
{
	if (cat->nb_values >= sizeof_array(cat->values)) {
		warning("Too many values for category %s", cat->name);
		return;
	}
	struct cat_value *cat_value = cat->values + cat->nb_values;
	cat_value->hf = hf;
	cat_value->cat = cat;
	cat->values[cat->nb_values++].value = parameter_suppress(hf->value);
}

static struct category *category_lookup(struct contact_view *ctv, char const *name)
{
	struct category *cat;
	LIST_FOREACH(cat, &ctv->categories, entry) {
		if (0 == strcasecmp(name, cat->name)) return cat;
	}
	return NULL;
}

static void add_categorized_value(struct contact_view *ctv, char const *cat_name, struct header_field *hf)
{
	struct category *cat = category_lookup(ctv, cat_name);
	if (! cat) {
		if_fail (cat = category_new(ctv, cat_name)) return;
	}
	category_add_value(cat, hf);
}

static void category_dtor(struct category *cat)
{
	free(cat->name);
	cat->name = NULL;
	while (cat->nb_values--) {
		FreeIfSet(&cat->values[cat->nb_values].value);
	}
	LIST_REMOVE(cat, entry);
}

static void category_del(struct category *cat)
{
	category_dtor(cat);
	free(cat);
}

static void contact_view_reload(struct contact_view *ctv);
// gives the field value to edit (and replace), or NULL for a new entry
static void edit_or_add(struct contact_view *ctv, struct cat_value *cat_value)
{
	struct contact *const ct = msg2contact(ctv->view.msg);
	if (ct->msg.version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot edit a transient contact");
		return;
	}
	struct field_dialog *fd = field_dialog_new(GTK_WINDOW(ctv->view.view.window),
		cat_value ? cat_value->cat->name : "",
		cat_value ? cat_value->hf->name : "",
		cat_value ? cat_value->value : "");
	on_error goto err;
	// Run the dialog untill the user either cancels or saves
	if (gtk_dialog_run(GTK_DIALOG(fd->dialog)) == GTK_RESPONSE_ACCEPT) {
		// Edit the header within the contact struct
		// (do not request a patch untill the user saves this page)
		struct varbuf new_value;
		if_fail (varbuf_ctor(&new_value, 1024, true)) goto err;
		varbuf_append_strs(&new_value,
			gtk_entry_get_text(GTK_ENTRY(fd->value_entry)),
			"; category=\"", gtk_combo_box_get_active_text(GTK_COMBO_BOX(fd->cat_combo)), "\"",
			NULL);
		char const *field_name = gtk_combo_box_get_active_text(GTK_COMBO_BOX(fd->field_combo));
		if (cat_value) {	// this is a replacement
			header_field_set(cat_value->hf, field_name, new_value.buf);
		} else {	// a new field
			(void)header_field_new(ct->msg.header, field_name, new_value.buf);
		}
		varbuf_dtor(&new_value);
		on_error goto err;
		// Rebuild our view in order to show the change
		contact_view_reload(ctv);
	}
	field_dialog_del(fd);
	return;
err:
	alert_error();
}

static void edit_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct cat_value *cat_value = (struct cat_value *)data;
	struct contact_view *const ctv = cat_value->cat->ctv;
	edit_or_add(ctv, cat_value);
}

static void add_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	edit_or_add(ctv, NULL);
}

static void del_field_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct cat_value *cat_value = (struct cat_value *)data;
	struct contact_view *const ctv = cat_value->cat->ctv;
	if (ctv->view.msg->version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot edit a transient contact");
		return;
	}
	if (confirm("Delete this value ?")) {
		header_field_del(cat_value->hf);
		contact_view_reload(ctv);
	}
}

static GtkWidget *category_widget(struct category *cat, bool editable)
{
	GtkWidget *table = gtk_table_new(editable ? 4:2, cat->nb_values, FALSE);
	for (unsigned v = 0; v < cat->nb_values; v++) {
		GtkWidget *field_label = gtk_label_new(NULL);
		char *markup = g_markup_printf_escaped("<i>%s</i> : ", cat->values[v].hf->name);
		gtk_label_set_markup(GTK_LABEL(field_label), markup);
		g_free(markup);
		gtk_table_attach(GTK_TABLE(table), field_label, 0, 1, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 0, 0);
		gtk_table_attach(GTK_TABLE(table), gtk_label_new(cat->values[v].value), 1, 2, v, v+1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
		if (editable) {
			GtkWidget *edit_button = gtk_button_new_from_stock(GTK_STOCK_EDIT);
			g_signal_connect(edit_button, "clicked", G_CALLBACK(edit_cb), cat->values+v);
			gtk_table_attach(GTK_TABLE(table), edit_button, 2, 3, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 0, 0);
			GtkWidget *del_button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
			g_signal_connect(del_button, "clicked", G_CALLBACK(del_field_cb), cat->values+v);
			gtk_table_attach(GTK_TABLE(table), del_button, 3, 4, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 0, 0);
		}
	}
	return make_frame(cat->name, table);
}

static void contact_view_empty(struct contact_view *ctv)
{
	debug("ctv@%p", ctv);
	struct category *cat;
	while (NULL != (cat = LIST_FIRST(&ctv->categories))) {
		category_del(cat);
	}
	if (ctv->view.view.window) {
		empty_container(ctv->view.view.window);
	}
}

static void contact_view_dtor(struct contact_view *ctv)
{
	debug("ctv@%p", ctv);
	contact_view_empty(ctv);
	sc_msg_view_dtor(&ctv->view);
}

static void contact_view_del(struct sc_view *view)
{
	struct contact_view *ctv = DOWNCAST(view2msg_view(view), view, contact_view);
	contact_view_dtor(ctv);
	free(ctv);
}

static void save_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	struct contact *const ct = msg2contact(ctv->view.msg);
	debug("saving contact '%s'", ct->name);
	if (ct->msg.version != 0) {
		if_fail (mdir_del_request(&ct->msg.mdirb->mdir, ct->msg.version)) {
			alert_error();
			return;
		}
	}
	if_fail (mdir_patch_request(&ct->msg.mdirb->mdir, MDIR_ADD, ct->msg.header)) alert_error();
	struct mdirb *mdirb = ctv->view.msg->mdirb;	// save it because we are going to destroy ctv
	gtk_widget_destroy(ctv->view.view.window);
	mdirb_refresh(mdirb);
}

static void cancel_cb(GtkToolButton *button, gpointer data)
{
	(void)button;
	struct contact_view *const ctv = (struct contact_view *)data;
	debug("cancel");
	if (confirm("Forget all about him ?")) {
		gtk_widget_destroy(ctv->view.view.window);
	}
}


static void del_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	if (confirm("Delete this contact ?")) {
		if_fail (mdir_del_request(&ctv->view.msg->mdirb->mdir, ctv->view.msg->version)) {
			alert_error();
			return;
		}
		gtk_widget_destroy(ctv->view.view.window);
	}
}

static void contact_rename(struct contact *ct, char const *new_name)
{
	struct header_field *hf = header_find(ct->msg.header, SC_NAME_FIELD, NULL);
	assert(hf);
	header_field_set(hf, NULL, new_name);
	ct->name = hf->value;
}

static void rename_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	struct contact *const ct = msg2contact(ctv->view.msg);
	if (ct->msg.version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot edit a transient contact");
		return;
	}
	GtkWidget *name_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(name_entry), ct->name);
	struct sc_dialog *dialog = sc_dialog_new("Rename Contact",
		GTK_WINDOW(ctv->view.view.window),
		make_labeled_hbox("Name", name_entry));
	
	// Run the dialog untill the user either cancels or saves
	if (sc_dialog_accept(dialog)) {
		// Edit the header within the contact struct
		// (do not request a patch untill the user saves this page)
		contact_rename(ct, gtk_entry_get_text(GTK_ENTRY(name_entry)));
		// Rebuild our view in order to show the change
		contact_view_reload(ctv);
	}
	sc_dialog_del(dialog);
}

static void contact_view_fill(struct contact_view *ctv)
{
	struct contact *const ct = msg2contact(ctv->view.msg);
	debug("ctv@%p for contact %s", ctv, ct->name);
	LIST_INIT(&ctv->categories);
	
	// Header with photo and name
	char fname[PATH_MAX];
	struct header_field *picture_field = header_find(ct->msg.header, "sc-picture", NULL);
	if (picture_field) {
		if_fail ((void)chn_get_file(&ccnx, fname, picture_field->value)) {
			picture_field = NULL;
			error_clear();
		}
	}
	GtkWidget *photo = picture_field ?
		gtk_image_new_from_file(fname) :
		gtk_image_new_from_stock(
#			if TRUE == GTK_CHECK_VERSION(2, 10, 0)
			GTK_STOCK_ORIENTATION_PORTRAIT,
#			else
			GTK_STOCK_NEW,
#			endif
		GTK_ICON_SIZE_DIALOG);

	GtkWidget *name_label = gtk_label_new(NULL);
	char *mark_name = g_markup_printf_escaped("<span size=\"x-large\"><b>%s</b></span>", ct->name);
	gtk_label_set_markup(GTK_LABEL(name_label), mark_name);
	g_free(mark_name);
	

	GtkWidget *page = gtk_vbox_new(FALSE, 0);
	GtkWidget *head_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head_hbox), photo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head_hbox), name_label, TRUE, TRUE, 0);
	if (ct->msg.version >= 0) {
		GtkWidget *edit_button = gtk_button_new_from_stock(GTK_STOCK_EDIT);
		g_signal_connect(edit_button, "clicked", G_CALLBACK(rename_cb), ctv);
		gtk_box_pack_end(GTK_BOX(head_hbox), edit_button, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(page), head_hbox, FALSE, FALSE, 0);
	
	// Category boxes
	struct header_field *hf;
	LIST_FOREACH(hf, &ct->msg.header->fields, entry) {
		char *cat_name = parameter_extract(hf->value, "category");
		if (! cat_name) continue;
		add_categorized_value(ctv, cat_name, hf);
		free(cat_name);
		on_error return;
	}
	struct category *cat;
	LIST_FOREACH(cat, &ctv->categories, entry) {
		gtk_box_pack_start(GTK_BOX(page), category_widget(cat, ct->msg.version >= 0), FALSE, FALSE, 0);
	}
	// Then a toolbar
	GtkWidget *toolbar;
	if (ct->msg.version > 0) {	// synched
		toolbar = make_toolbar(4,
			GTK_STOCK_ADD,  add_cb, ctv,
			GTK_STOCK_SAVE, save_cb, ctv,
			GTK_STOCK_DELETE, del_cb, ctv,
			GTK_STOCK_QUIT, close_cb, ctv->view.view.window);
	} else if (ct->msg.version < 0) {	// transient : not editable
		toolbar = make_toolbar(1, GTK_STOCK_QUIT, close_cb, ctv->view.view.window);
	} else {	// v=0 -> new contact
		toolbar = make_toolbar(3,
			GTK_STOCK_ADD,  add_cb, ctv,
			GTK_STOCK_SAVE, save_cb, ctv,
			GTK_STOCK_CANCEL, cancel_cb, ctv);
	}
	gtk_box_pack_end(GTK_BOX(page), toolbar, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(ctv->view.view.window), page);
	gtk_widget_show_all(page);
}

static struct sc_plugin plugin;
static void contact_view_ctor(struct contact_view *ctv, struct contact *ct)
{
	debug("ctv@%p", ctv);
	GtkWidget *window = make_window(WC_VIEWER, NULL, NULL);
	sc_msg_view_ctor(&ctv->view, &plugin, &ct->msg, window);
	contact_view_fill(ctv);
}

static struct sc_msg_view *contact_view_new(struct sc_msg *msg)
{
	struct contact_view *ctv = Malloc(sizeof(*ctv));
	if_fail (contact_view_ctor(ctv, msg2contact(msg))) {
		free(ctv);
		ctv = NULL;
	}
	return &ctv->view;
}

static void contact_view_reload(struct contact_view *ctv)
{
	contact_view_empty(ctv);
	contact_view_fill(ctv);
}

static void contact_new_cb(struct mdirb *mdirb, char const *name, GtkWindow *parent)
{
	(void)parent;
	debug("Add a contact in dir %s", name);

	// Create the contact
	struct header *h = header_new();
	(void)header_field_new(h, SC_TYPE_FIELD, SC_CONTACT_TYPE);
	(void)header_field_new(h, SC_NAME_FIELD, "Unnamed");
	struct sc_msg *msg = contact_new(mdirb, h, 0);
	header_unref(h);
	assert(msg);

	// And run a view window for it
	(void)contact_view_new(msg);
	on_error alert_error();
	sc_msg_unref(msg);	// make the view the only ref to this message
}

/*
 * Contact Picker
 */

static void picker_button_cb(GtkButton *button, gpointer *user_data)
{
	(void)button;
	struct contact_picker *picker = (struct contact_picker *)user_data;

	// The list of contacts
	enum {
		PICKER_FIELD_NAME,
		PICKER_FIELD_CONTACT,
		NB_PICKER_FIELDS,
	};
	GtkTreeStore *store = gtk_tree_store_new(NB_PICKER_FIELDS, G_TYPE_STRING, G_TYPE_POINTER);
	GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
	GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
		gtk_tree_view_column_new_with_attributes("Name", text_renderer, "text", PICKER_FIELD_NAME, NULL));
	struct contact *ct;
	GtkTreeIter iter;
	LIST_FOREACH(ct, &contacts, entry) {
		gtk_tree_store_append(store, &iter, NULL);
		gtk_tree_store_set(store, &iter, PICKER_FIELD_NAME, ct->name, PICKER_FIELD_CONTACT, ct, -1);
	}
	
	// The dialog
	struct sc_dialog *dialog = sc_dialog_new("Choose a Contact", picker->parent, make_scrollable(tree));
	if (sc_dialog_accept(dialog)) {
		GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
		if (TRUE == gtk_tree_selection_get_selected(selection, NULL, &iter)) {
			GValue gptr;
			memset(&gptr, 0, sizeof(gptr));
			gtk_tree_model_get_value(GTK_TREE_MODEL(store), &iter, PICKER_FIELD_CONTACT, &gptr);
			assert(G_VALUE_HOLDS_POINTER(&gptr));
			ct = g_value_get_pointer(&gptr);
			g_value_unset(&gptr);
			picker->cb(picker, ct);
		}
	}

	sc_dialog_del(dialog);
}

void contact_picker_ctor(struct contact_picker *picker, contact_picker_cb *cb, GtkWindow *parent)
{
	picker->button = gtk_button_new_from_stock(GTK_STOCK_ORIENTATION_PORTRAIT);
	picker->cb = cb;
	picker->parent = parent,
	g_signal_connect(picker->button, "clicked", G_CALLBACK(picker_button_cb), picker);
}

void contact_picker_dtor(struct contact_picker *picker)
{
	if (picker->button) {
		gtk_widget_destroy(picker->button);
		picker->button = NULL;
	}
}

/*
 * Init
 */

static struct sc_plugin_ops const ops = {
	.msg_new      = contact_new,
	.msg_del      = contact_del,
	.msg_descr    = contact_descr,
	.msg_icon     = contact_icon,
	.msg_view_new = contact_view_new,
	.msg_view_del = contact_view_del,
	.dir_view_new = NULL,
	.dir_view_del = NULL,
};
static struct sc_plugin plugin = {
	.name = "contact",
	.type = SC_CONTACT_TYPE,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 1,
	.dir_functions = {
		{ NULL, "New", contact_new_cb },
	},
};

void contact_init(void)
{
	sc_plugin_register(&plugin);
}
