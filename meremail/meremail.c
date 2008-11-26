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
#include <stdio.h>
#include <pth.h>
#include "meremail.h"

int main(int nb_args, char *args[])
{
	if_fail(init("meremail", nb_args, args)) return EXIT_FAILURE;
	GtkWidget *folder_window = make_folder_window("/");
	if (! folder_window) return EXIT_FAILURE;
	exit_when_closed(folder_window);
	gtk_widget_show_all(folder_window);
	gtk_main();
	return EXIT_SUCCESS;
}
