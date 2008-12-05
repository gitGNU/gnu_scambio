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
#include "merelib.h"
#include "meremail.h"
#include "scambio/channel.h"

/*
 * Make a Window to compose a mail, with optional parameters for replying to another mail
 */

GtkWidget *make_compose_window(char const *from, char const *to, char const *subject)
{
	debug("from=%s, to=%s, subject=%s", from, to, subject);
	GtkWidget *win = make_window(NULL);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(win), vbox);
	// From : combobox with all accepted from addresses (with from param preselected)
	// To : input text (with latter a button to pick a contact)
	// Subject : input text
	// - a small text editor (empty, without requote : this is a handheld device)
	//   (later : add text button that launches an external text editor)
	// Then some buttons :
	// - add file (later an editable list of joined files)
	// - cancel, send

	GtkWidget *toolbar = make_toolbar(3,
		GTK_STOCK_JUMP_TO, NULL,     NULL,	// Forward
		GTK_STOCK_DELETE,  NULL,     NULL,	// Delete
		GTK_STOCK_QUIT,    close_cb, win);

#	ifdef WITH_MAEMO
	hildon_window_add_toolbar(HILDON_WINDOW(win), toolbar);
#	else
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);
#	endif

	return win;
}
