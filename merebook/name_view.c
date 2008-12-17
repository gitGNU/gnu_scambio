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
static void name_dialog_ctor(struct name_dialog *nd, GtkWindow *parent, char const *name)
{
	nd->dialog = gtk_dialog_new_with_buttons("Rename Contact", parent,
		GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	nd->name_entry = gtk_entry_new();
	if (name) gtk_entry_set_text(GTK_ENTRY(nd->name_entry), name);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(nd->dialog));
	gtk_container_add(GTK_CONTAINER(content_area), make_labeled_hboxes(1,
		"Name", nd->name_entry));

	gtk_widget_show_all(nd->dialog);
}

struct name_dialog *name_dialog_new(GtkWindow *parent, char const *name)
{
	struct name_dialog *nd = Malloc(sizeof(*nd));
	name_dialog_ctor(nd, parent, name);
	return nd;
}

static void name_dialog_dtor(struct name_dialog *nd)
{
	gtk_widget_destroy(nd->dialog);
}

void name_dialog_del(struct name_dialog *nd)
{
	name_dialog_dtor(nd);
	free(nd);
}

