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
#ifndef MAIL_H_081222
#define MAIL_H_081222
#include <time.h>
#include "merelib.h"
#include "vadrouille.h"

extern struct mdir *mail_outbox;
void mail_init(void);

struct mail_composer {
	struct sc_view view;
	GtkWidget *from_combo, *to_entry, *subject_entry, *editor;
	unsigned nb_files;
	char filenames[32][PATH_MAX];
};

struct sc_view *mail_composer_new(char const *from, char const *to, char const *subject);

#endif
