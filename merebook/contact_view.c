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
#include "merelib.h"

/*
 * Categories
 */

struct category {
	LIST_ENTRY(category) entry;
	char *name;
	unsigned nb_values;
	struct cat_value {
		char const *field;
		struct varbuf value;
	} values[32];
};

LIST_HEAD(categories, category);

static void category_ctor(struct category *cat, struct categories *cats, char const *name)
{
	cat->nb_values = 0;
	if_fail (cat->name = Strdup(name)) return;
	LIST_INSERT_HEAD(cats, cat, entry);
}

static struct category *category_new(struct categories *cats, char const *name)
{
	struct category *cat = Malloc(sizeof(*cat));
	on_error return NULL;
	if_fail (category_ctor(cat, cats, name)) {
		free(cat);
		cat = NULL;
	}
	return cat;
}

static void category_add_value(struct category *cat, char const *field, char const *value)
{
	if (cat->nb_values >= sizeof_array(cat->values)) {
		warning("Too many values for category %s, skipping value '%s'", cat->name, value);
		return;
	}
	struct cat_value *cat_value = cat->values + cat->nb_values;
	cat_value->field = field;
#	define MAX_VALUE_LEN 10000	// FIXME (stripped value into a varbuf ?)
	if_fail (varbuf_ctor(&cat_value->value, MAX_VALUE_LEN, true)) return;
	if_fail (varbuf_put(&cat_value->value, MAX_VALUE_LEN)) return;
	size_t value_len;
	if_fail (value_len = header_stripped_value(value, MAX_VALUE_LEN, cat_value->value.buf)) return;
	if_fail (varbuf_append(&cat_value->value, value_len, value)) {
		varbuf_dtor(&cat_value->value);
		return;
	}
	varbuf_stringify(&cat_value->value);
	cat->nb_values++;
}

static struct category *category_lookup(struct categories *cats, char const *name)
{
	struct category *cat;
	LIST_FOREACH(cat, cats, entry) {
		if (0 == strcmp(name, cat->name)) return cat;
	}
	return NULL;
}

static void add_categorized_value(struct categories *cats, char const *cat_name, char const *field, char const *value)
{
	struct category *cat = category_lookup(cats, cat_name);
	if (! cat) {
		if_fail (cat = category_new(cats, cat_name)) return;
	}
	category_add_value(cat, field, value);
}

static void category_dtor(struct category *cat)
{
	free(cat->name);
	cat->name = NULL;
	while (cat->nb_values--) {
		varbuf_dtor(&cat->values[cat->nb_values].value);
	}
	LIST_REMOVE(cat, entry);
}

static void category_del(struct category *cat)
{
	category_dtor(cat);
	free(cat);
}

static GtkWidget *category_widget(struct category *cat)
{
	GtkWidget *table = gtk_table_new(2, cat->nb_values, FALSE);
	for (unsigned v = 0; v < cat->nb_values; v++) {
		GtkWidget *field_label = gtk_label_new(NULL);
		char *markup = g_markup_printf_escaped("<i>%s</i> : ", cat->values[v].field);
		gtk_label_set_markup(GTK_LABEL(field_label), markup);
		g_free(markup);
		gtk_table_attach(GTK_TABLE(table), field_label, 0, 1, v, v+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 1, 1);
		gtk_table_attach(GTK_TABLE(table), gtk_label_new(cat->values[v].value.buf), 1, 2, v, v+1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 1, 1);
	}
	return make_frame(cat->name, table);
}

/*
 * View context
 */

struct contact_view {
	struct categories categories;
	struct contact *ct;
	GtkWidget *window;
};

static void contact_view_dtor(struct contact_view *ctv)
{
	debug("ctv@%p", ctv);
	struct category *cat;
	while (NULL != (cat = LIST_FIRST(&ctv->categories))) {
		category_del(cat);
	}
	gtk_widget_destroy(ctv->window);
}

static void contact_view_del(struct contact_view *ctv)
{
	contact_view_dtor(ctv);
	free(ctv);
}

static void del_me(GtkWidget *widget, gpointer data)
{
	(void)widget;
	struct contact_view *ctv = (struct contact_view *)data;
	contact_view_del(ctv);
}

static void edit_cb(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;
}

static void contact_view_ctor(struct contact_view *ctv, struct contact *ct)
{
	debug("ctv@%p", ctv);
	LIST_INIT(&ctv->categories);
	ctv->ct = ct;
	ctv->window = make_window(del_me, ctv);
	GtkWidget *page = gtk_vbox_new(FALSE, 0);
	
	// Header with photo and name
	char fname[PATH_MAX];
	char const *picture_field = header_search(ct->header, "sc-picture");
	if (picture_field) {
		if_fail ((void)chn_get_file(&ccnx, fname, picture_field)) {
			picture_field = NULL;
			error_clear();
		}
	}
	GtkWidget *photo = picture_field ?
		gtk_image_new_from_file(fname) :
		gtk_image_new_from_stock(GTK_STOCK_ORIENTATION_PORTRAIT, GTK_ICON_SIZE_DIALOG);

	GtkWidget *name_label = gtk_label_new(NULL);
	char *mark_name = g_markup_printf_escaped("<span size=\"x-large\"><b>%s</b></span>", ct->name);
	gtk_label_set_markup(GTK_LABEL(name_label), mark_name);
	g_free(mark_name);
	
	GtkWidget *head_hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head_hbox), photo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(head_hbox), name_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(page), head_hbox, FALSE, FALSE, 0);
	
	// Category boxes
	unsigned nb_fields = header_nb_fields(ct->header);
	for (unsigned f = 0; f < nb_fields; f++) {
		struct head_field const *hf = header_field(ct->header, f);
		char cat_name[128];
		if_fail (header_copy_parameter("category", hf->value, sizeof(cat_name), cat_name)) {
			error_clear();
			continue;
		}
		if_fail (add_categorized_value(&ctv->categories, cat_name, hf->name, hf->value)) return;
	}
	struct category *cat;
	LIST_FOREACH(cat, &ctv->categories, entry) {
		gtk_box_pack_start(GTK_BOX(page), category_widget(cat), FALSE, FALSE, 0);
	}
	// Then a toolbar
	GtkWidget *toolbar = make_toolbar(2,
		GTK_STOCK_EDIT, edit_cb, ctv,
		GTK_STOCK_QUIT, close_cb, ctv->window);
	gtk_box_pack_end(GTK_BOX(page), toolbar, FALSE, FALSE, 0);
		
	gtk_container_add(GTK_CONTAINER(ctv->window), page);
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

/*
 * Build view
 */

GtkWidget *make_contact_window(struct contact *ct)
{
	struct contact_view *ctv = contact_view_new(ct);
	on_error return NULL;
	return ctv->window;
}
