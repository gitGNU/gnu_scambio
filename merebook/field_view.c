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
#include <strings.h>
#include "misc.h"
#include "scambio.h"
#include "merebook.h"
#include "merelib.h"

// Throw no error.
static void field_dialog_ctor(struct field_dialog *fd, GtkWindow *parent, char const *cat_name, char const *field_name, char const *value)
{
	fd->dialog = gtk_dialog_new_with_buttons("Edit entry", parent,
		GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	// Category chooser
	fd->cat_combo = gtk_combo_box_entry_new_text();
	static char const *cat_names[] = { "home", "work" };	// FIXME
	for (unsigned n = 0; n < sizeof_array(cat_names); n++) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(fd->cat_combo), cat_names[n]);
		if (cat_name && strcasecmp(cat_names[n], cat_name) == 0) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(fd->cat_combo), n);
		}
	}

	// Field name chooser
	fd->field_combo = gtk_combo_box_entry_new_text();
	static char const *field_names[] = { "email", "phone", "address" };	// FIXME
	for (unsigned n = 0; n < sizeof_array(field_names); n++) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(fd->field_combo), field_names[n]);
		if (field_name && strcasecmp(field_names[n], field_name) == 0) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(fd->field_combo), n);
		}
	}

	fd->value_entry = gtk_entry_new();
	if (value) gtk_entry_set_text(GTK_ENTRY(fd->value_entry), value);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(fd->dialog));
	gtk_container_add(GTK_CONTAINER(content_area), make_labeled_hboxes(3,
		"Category", fd->cat_combo,
		"Field", fd->field_combo,
		"Value", fd->value_entry));

	gtk_widget_show_all(fd->dialog);
}

struct field_dialog *field_dialog_new(GtkWindow *parent, char const *cat_name, char const *field_name, char const *value)
{
	struct field_dialog *fd = Malloc(sizeof(*fd));
	field_dialog_ctor(fd, parent, cat_name, field_name, value);
	return fd;
}

static void field_dialog_dtor(struct field_dialog *fd)
{
	gtk_widget_destroy(fd->dialog);
}

void field_dialog_del(struct field_dialog *fd)
{
	field_dialog_dtor(fd);
	free(fd);
}

