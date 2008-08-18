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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <log.h>
#include "stribution.h"
#include "header.h"

static void my_print(char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

int main(void)
{
	struct stribution *strib = strib_new("conf1.sample");
	assert(strib);
	strib_dump(strib, my_print);
	// Now add a header
	char *head_mutable = strdup(
		"From: grosminet@acme.org\n"
		"Truc: Muche\n"
		"Machin: Chose\n"
		"Content-Length: 7\n"
		"Subject: [TODO] manger titi\n"
	);
	struct header *head;
	assert(0 == header_new(&head));
	assert(0 == header_parse(head, head_mutable));
	struct strib_action const *actions[strib->nb_tests];
	unsigned nb_actions = strib_eval(strib, head, actions);
	assert(nb_actions == 3);
	header_del(head);
	free(head_mutable);
	strib_del(strib);
	return EXIT_SUCCESS;
}
