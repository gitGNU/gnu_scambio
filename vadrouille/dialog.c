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
#include "merelib.h"
#include "vadrouille.h"
#include "dialog.h"

// Throw no error.
static void sc_dialog_ctor(struct sc_dialog *view, char const *title, GtkWindow *parent, GtkWidget *content)
{
	view->dialog = gtk_dialog_new_with_buttons(title, parent,
		GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		NULL);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(view->dialog));
	gtk_container_add(GTK_CONTAINER(content_area), content);

	gtk_widget_show_all(view->dialog);
}

struct sc_dialog *sc_dialog_new(char const *title, GtkWindow *parent, GtkWidget *content)
{
	struct sc_dialog *view = Malloc(sizeof(*view));
	sc_dialog_ctor(view, title, parent, content);
	return view;
}

static void sc_dialog_dtor(struct sc_dialog *view)
{
	gtk_widget_destroy(view->dialog);
}

void sc_dialog_del(struct sc_dialog *dialog)
{
	sc_dialog_dtor(dialog);
	free(dialog);
}

bool sc_dialog_accept(struct sc_dialog *view)
{
	return gtk_dialog_run(GTK_DIALOG(view->dialog)) == GTK_RESPONSE_ACCEPT;
}

