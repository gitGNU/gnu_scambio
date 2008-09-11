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
/* A mean to access to a persistant, immutable configuration.
 * The static configuration is a set of key=>value, where a value may be
 * a string or a integer.
 * This is recommended to use commands instead of static configuration file
 * for configuration purpose, so that configuration can be change in real time
 * while the system is running.
 */
#ifndef CONF_H_080617
#define CONF_H_080617

#define DEFAULT_MDIRD_PORT 21654

void conf_set_default_str(char const *name, char const *value);
void conf_set_default_int(char const *name, long long value);

// abort if not found !
// set default values first !
char const *conf_get_str(char const *name);
long long conf_get_int(char const *name);

#endif
