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
#include <stdbool.h>
#include <string.h>
#include "scambio.h"
#include "options.h"

static bool help, version;
static struct option common_opts[] = {
	{ 'h', "help",    OPT_FLAG, &help,    "short options description", {}, },
	{ 'v', "version", OPT_FLAG, &version, "display version info and exit", {}, },
};
static bool swallow_next_arg;

static void read_param(char const *value, struct option const *option)
{
	if (option->type != OPT_FLAG && ! value) {
		with_error(0, "Missing value for option %s", option->long_opt) return;
	}
	switch (option->type) {
		char *end;
		case OPT_FLAG:
			*(bool *)option->value = true;
			break;
		case OPT_STRING:
			*(char const **)option->value = value;
			break;
		case OPT_INT:
			*(int *)option->value = strtol(value, &end, 0);
			if (*end != '\0') with_error(0, "Bad integer (%s) for option %s", value, option->long_opt) return;
			break;
		case OPT_ENUM:
			*(int *)option->value = -1;
			for (int e = 0; option->type_opts.opt_enum[e]; e++) {
				if (0 == strcmp(option->type_opts.opt_enum[e], value)) {
					*(int *)option->value = e;
					break;
				}
			}
			if (*(int *)option->value == -1) {
				with_error(0, "Unknown value (%s) for option %s", value, option->long_opt) return;
			}
			break;
	}
}

static bool parse_single(char const *arg, char const *next_arg, struct option const *options, unsigned nb_options)
{
	unsigned o;
	for (o = 0; o < nb_options; o++) {
		struct option const *const opt = options+o;
		if (arg[0] == '-' && arg[1] == opt->short_opt && arg[2] == '\0') {
			if_fail (read_param(next_arg, opt)) return false;
			if (opt->type != OPT_FLAG) swallow_next_arg = true;
			return true;
		}
		int opt_len = strlen(opt->long_opt);
		if (arg[0] == '-' && arg[1] == '-' && 0 == strncmp(opt->long_opt, arg+2, opt_len)) {
			char const *value = arg+2+opt_len;
			debug("value = %s", value);
			if (*value == '=') value++;
			else if (*value == '\0') {
				value = next_arg;
				if (opt->type != OPT_FLAG) swallow_next_arg = true;
			}
			if_fail (read_param(value, opt)) return false;
			return true;
		}
	}
	return false;
}

static void display_help_(struct option const *options, unsigned nb_options)
{
	// TODO: display default values
	for (unsigned o = 0; o < nb_options; o++) {
		struct option const *const opt = options+o;
		switch (opt->type) {
			case OPT_FLAG:
				printf("-%c, --%s : %s\n", opt->short_opt, opt->long_opt, opt->help);
				break;
			case OPT_STRING:
				printf("-%c string, --%s=string : %s\n", opt->short_opt, opt->long_opt, opt->help);
				break;
			case OPT_INT:
				printf("-%c N, --%s=N : %s\n", opt->short_opt, opt->long_opt, opt->help);
				break;
			case OPT_ENUM:
				printf("-%c string, --%s=N : %s\n", opt->short_opt, opt->long_opt, opt->help);
				break;
		}
	}
}
static void display_help(struct option const *options, unsigned nb_options)
{
	printf("syntax :\n");
	display_help_(options, nb_options);
	display_help_(common_opts, sizeof_array(common_opts));
}

static void display_version(void)
{
	puts("Version "VERSION);
}

unsigned option_parse(int nb_args, char const *const *args, struct option const *options, unsigned nb_options)
{
	int a;
	help = false;
	version = false;
	for (a = 1; a < nb_args; a ++) {
		char const *const arg = args[a];
		if (arg[0] == '-' && arg[1] == '\0') break;
		swallow_next_arg = false;
		char const *const next_arg = a < nb_args-1 ? args[a+1] : NULL;
		bool parsed;
		if_fail (parsed = parse_single(arg, next_arg, options, nb_options)) return a;
		if (! parsed) {
			if_fail (parsed = parse_single(arg, next_arg, common_opts, sizeof_array(common_opts))) return a;
			if (! parsed) with_error(0, "Unknown option '%s'", arg) return a;
			if (help) display_help(options, nb_options);
			if (version) display_version();
			exit(0);
		}
		if (swallow_next_arg) a++;
	}
	return a;
}

void option_missing(char const *opt)
{
	error("missing option : '%s'", opt);
	info("use --help for more help");
	exit(1);
}
