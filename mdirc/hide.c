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
#include "scambio.h"
#include "hide.h"

void hide_begin(void)
{
}
void hide_end(void)
{
}

struct hide_cfg *hide_cfg_get(char const *path)
{
	(void)path;
	return NULL;
}

void hide_cfg_release(struct hide_cfg *cfg)
{
	(void)cfg;
}

bool show_this_dir(struct hide_cfg *cfg, char const *name)
{
	(void)cfg;
	(void)name;
	return true;
}
