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
/* The contact view :
 * - a header with the id picture and the name (sc-picture, sc-name)
 * - then a box per category (category is the first-part of a field name, like work or home or whatever - sc being for internal use)
 * - Resources are not shown until the user clicks on them (download only the picture at first).
 */
#include <string.h>
#include <limits.h>
#include "scambio.h"
#include "scambio/channel.h"
#include "merebook.h"
#include "merelib.h"
#include "varbuf.h"
#include "misc.h"

/*
 * Data Definitions
 */

struct contact_view {
	LIST_HEAD(categories, category) categories;
	struct contact *ct;
	GtkWidget *window;
};
static void contact_view_reload(struct contact_view *ctv);

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

/*
 * Categories
 */

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

// gives the field value to edit (and replace), or NULL for a new entry
static void edit_or_add(struct contact_view *ctv, struct cat_value *cat_value)
{
	if (ctv->ct->version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot edit a transient contact");
		return;
	}
	struct field_dialog *fd = field_dialog_new(GTK_WINDOW(ctv->window),
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
			(void)header_field_new(ctv->ct->header, field_name, new_value.buf);
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
	if (ctv->ct->version < 0) {
		alert(GTK_MESSAGE_ERROR, "Cannot edit a transient contact");
		return;
	}
	if (confirm("Delete this value ?")) {
		header_field_del(cat_value->hf);
		contact_view_reload(ctv);
	}
}

static GtkWidget *category_widget(struct category *cat)
{
	GtkWidget *table = gtk_table_new(4, cat->nb_values, FALSE);
	for (unsigned v = 0; v < cat->nb_values; v++) {
		GtkWidget *field_label = gtk_label_new(NULL);
		char *markup = g_markup_printf_escaped("<i>%s</i> : ", cat->values[v].hf->name);
		gtk_label_set_markup(GTK_LABEL(field_label), markup);
		g_free(markup);
		gtk_table_attach(GTK_TABLE(table), field_label, 0, 1, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 0, 0);
		gtk_table_attach(GTK_TABLE(table), gtk_label_new(cat->values[v].value), 1, 2, v, v+1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
		GtkWidget *edit_button = gtk_button_new_from_stock(GTK_STOCK_EDIT);
		g_signal_connect(edit_button, "clicked", G_CALLBACK(edit_cb), cat->values+v);
		gtk_table_attach(GTK_TABLE(table), edit_button, 2, 3, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 0, 0);
		GtkWidget *del_button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
		g_signal_connect(del_button, "clicked", G_CALLBACK(del_field_cb), cat->values+v);
		gtk_table_attach(GTK_TABLE(table), del_button, 3, 4, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 0, 0);
	}
	return make_frame(cat->name, table);
}

/*
 * Contact View
 */

static void contact_view_empty(struct contact_view *ctv)
{
	debug("ctv@%p", ctv);
	struct category *cat;
	while (NULL != (cat = LIST_FIRST(&ctv->categories))) {
		category_del(cat);
	}
	if (ctv->window) {
		empty_container(ctv->window);
	}
}

static void contact_view_dtor(struct contact_view *ctv)
{
	debug("ctv@%p", ctv);
	contact_view_empty(ctv);
	if (ctv->window) {	// may not be present
		gtk_widget_destroy(ctv->window);
		ctv->window = NULL;
	}
}

static void contact_view_del(struct contact_view *ctv)
{
	contact_view_dtor(ctv);
	free(ctv);
}

static void save_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	debug("saving contact '%s'", ctv->ct->name);
	if_fail (mdir_del_request(ctv->ct->book->mdir, ctv->ct->version)) {
		alert_error();
		return;
	}
	if_fail (mdir_patch_request(ctv->ct->book->mdir, MDIR_ADD, ctv->ct->header)) alert_error();
	gtk_widget_destroy(ctv->window);
	refresh_contact_list();
}

static void del_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	if (confirm("Delete this contact ?")) {
		if_fail (mdir_del_request(ctv->ct->book->mdir, ctv->ct->version)) {
			alert_error();
			return;
		}
		gtk_widget_destroy(ctv->window);
	}
}

// The window was destroyed for some reason : release the ref to it
static void unref_win(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *const ctv = (struct contact_view *)data;
	if (ctv->window) ctv->window = NULL;
	// If not for a window, what to live for ?
	contact_view_del(ctv);
}

static void contact_view_fill(struct contact_view *ctv)
{
	debug("ctv@%p", ctv);
	LIST_INIT(&ctv->categories);
	
	// Header with photo and name
	char fname[PATH_MAX];
	struct header_field *picture_field = header_find(ctv->ct->header, "sc-picture", NULL);
	if (picture_field) {
		if_fail ((void)chn_get_file(&ccnx, fname, picture_field->value)) {
			picture_field = NULL;
			error_clear();
		}
	}
	GtkWidget *photo = picture_field ?
		gtk_image_new_from_file(fname) :
		gtk_image_new_from_stock(GTK_STOCK_ORIENTATION_PORTRAIT, GTK_ICON_SIZE_DIALOG);

	GtkWidget *name_label = gtk_label_new(NULL);
	char *mark_name = g_markup_printf_escaped("<span size=\"x-large\"><b>%s</b></span>", ctv->ct->name);
	gtk_label_set_markup(GTK_LABEL(name_label), mark_name);
	g_free(mark_name);
	
	GtkWidget *page = gtk_vbox_new(FALSE, 0);
	GtkWidget *head_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head_hbox), photo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head_hbox), name_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(page), head_hbox, FALSE, FALSE, 0);
	
	// Category boxes
	struct header_field *hf;
	LIST_FOREACH(hf, &ctv->ct->header->fields, entry) {
		char *cat_name = parameter_extract(hf->value, "category");
		if (! cat_name) continue;
		add_categorized_value(ctv, cat_name, hf);
		free(cat_name);
		on_error return;
	}
	struct category *cat;
	LIST_FOREACH(cat, &ctv->categories, entry) {
		gtk_box_pack_start(GTK_BOX(page), category_widget(cat), FALSE, FALSE, 0);
	}
	// Then a toolbar
	GtkWidget *toolbar = ctv->ct->version > 0 ?
		make_toolbar(4,
			GTK_STOCK_ADD,  add_cb, ctv,
			GTK_STOCK_SAVE, save_cb, ctv,
			GTK_STOCK_DELETE, del_cb, ctv,
			GTK_STOCK_QUIT, close_cb, ctv->window) :
		make_toolbar(1, GTK_STOCK_QUIT, close_cb, ctv->window);
#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(ctv->window), toolbar);
#	else
	gtk_box_pack_end(GTK_BOX(page), toolbar, FALSE, FALSE, 0);
#	endif
	gtk_container_add(GTK_CONTAINER(ctv->window), page);
	gtk_widget_show_all(ctv->window);
}

static void contact_view_ctor(struct contact_view *ctv, struct contact *ct)
{
	debug("ctv@%p", ctv);
	ctv->ct = ct;
	ctv->window = make_window(unref_win, ctv);
	contact_view_fill(ctv);
}

static struct contact_view *contact_view_new(struct contact *ct)
{
	struct contact_view *ctv = Malloc(sizeof(*ctv));
	on_error return NULL;
	if_fail (contact_view_ctor(ctv, ct)) {
		free(ctv);
		ctv = NULL;
	}
	return ctv;
}

static void contact_view_reload(struct contact_view *ctv)
{
	contact_view_empty(ctv);
	contact_view_fill(ctv);
}

/*
 * Build view
 */

GtkWidget *make_contact_window(struct contact *ct)
{
	struct contact_view *ctv = contact_view_new(ct);
	on_error return NULL;
	return ctv->window;
}
