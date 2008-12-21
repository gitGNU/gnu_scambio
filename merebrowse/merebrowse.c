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
#include <pth.h>
#include "scambio.h"
#include "scambio/header.h"
#include "scambio.h"
#include "merelib.h"
#include "auth.h"
#include "misc.h"
#include "mdirb.h"
#include "browser.h"

/*
 * Main
 */

int main(int nb_args, char *args[])
{
	if_fail (init("merebrowse", nb_args, args)) return EXIT_FAILURE;
	if_fail (mdirb_init()) return EXIT_FAILURE;
	if_fail (browser_init()) return EXIT_FAILURE;

	struct browser *browser = browser_new("/");
	on_error return EXIT_FAILURE;
	exit_when_closed(browser->window);
	gtk_main();
	return EXIT_SUCCESS;
}

