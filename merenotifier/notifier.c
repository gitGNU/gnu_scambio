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
#include <pth.h>
#include "scambio.h"
#include "scambio/header.h"
#include "notif.h"
#include "display.h"

int main(void)
{
	pth_init();
	log_begin(NULL, NULL);
	error_begin();
	atexit(error_end);
	log_level = 5;
	if_fail (notif_begin()) return EXIT_FAILURE;
	atexit(notif_end);
	if_fail (display_begin()) return EXIT_FAILURE;
	atexit(display_end);

	while (! is_error()) {
		struct header *header;
		if_fail (header = header_new()) break;
		do {
			if_fail (header_read(header, 0)) break;
			// Now we've got a header : let's try to build a notification out of it
			struct notif *notif;
			if_fail (notif = notif_new_from_header(header)) break;
			if (! notif) break;
			display_refresh();
		} while (0);
		error_save();
		header_del(header);
		error_restore();
	}
	
	pth_kill();
	return EXIT_SUCCESS;
}
