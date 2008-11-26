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
#ifndef MERELIB_H_081008
#define MERELIB_H_081008

#include <stdbool.h>
#include <gtk/gtk.h>
#ifdef WITH_MAEMO
#include <hildon/hildon-program.h>
#include <gtk/gtkmain.h>
#endif

void init(char const *app_name, int nb_args, char *args[]);
void destroy_cb(GtkWidget *widget, gpointer data);
void alert(GtkMessageType type, char const *text);
bool confirm(char const *);
GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer));
GtkWidget *make_labeled_hbox(char const *label, GtkWidget *other);
GtkWidget *make_labeled_hboxes(unsigned nb_rows, ...);
GtkWidget *make_toolbar(unsigned nb_buttons, ...);

#endif
