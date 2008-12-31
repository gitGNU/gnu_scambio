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
#ifndef BROWSER_H_081222
#define BROWSER_H_081222

struct sc_plugin_dir_function;

struct browser {
	GtkWidget *window;
	GtkWidget *tree;
	GtkTreeStore *store;
	struct mdirb *mdirb;
	GtkTreeIter *iter;
	// This is a little odd but for the dir_function to be called we need these little structs
	unsigned nb_d2m;
	struct dirfunc2myself {
		struct sc_plugin_dir_function *function;
		struct browser *myself;
	} dirfunc2myself[16];
	unsigned nb_g2m;
	struct globfunc2myself {
		struct sc_plugin_global_function *function;
		struct browser *myself;
	} globfunc2myself[16];
};

void browser_init(void);
struct browser *browser_new(char const *folder);
void browser_del(struct browser *);
void browser_refresh(struct browser *browser);

#endif