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
#ifndef BROWSER_H_081220
#define BROWSER_H_081220

#include "mdirb.h"
#include "merelib.h"

struct browser {
	GtkWidget *window;
	GtkWidget *tree;
	GtkTreeStore *store;
	struct mdirb *mdirb;
	GtkTreeIter *iter;
};

void browser_init(void);
struct browser *browser_new(char const *root);
void browser_del(struct browser *);
void browser_refresh(struct browser *);

#endif
