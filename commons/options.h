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
#ifndef OPTIONS_H_081105
#define OPTIONS_H_081105

struct option {
	char short_opt;
	char const *long_opt;
	enum opt_type { OPT_FLAG, OPT_STRING, OPT_INT, OPT_ENUM } type;
	void *value;
	char const *help;
	union {
		char const *const *opt_enum;
	} type_opts;
};

// Return the index of the last arg parsed
unsigned option_parse(int nb_args, char const *const *args, struct option const *options, unsigned nb_options);
void option_missing(char const *opt);	// no return

#endif
