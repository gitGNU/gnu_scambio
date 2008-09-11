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
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "scambio.h"
#include "conf.h"

/* NOTICE: this should not log anything since log depends on conf */

/*
 * Data Definition
 */

/*
 * Configuration queries
 */

void conf_set_default_str(char const *name, char const *value)
{
	if (0 != setenv(name, value, 0)) error_push(errno, "Cannot setenv");
}

void conf_set_default_int(char const *name, long long value)
{
	int len = snprintf(NULL, 0, "%lld", value);
	char *str = malloc(len+1);	// is the string _copied_ into the environment ?
	if (! str) with_error(ENOMEM, "Cannot malloc for env value (len = %d)", len) return;
	snprintf(str, len+1, "%lld", value);
	conf_set_default_str(name, str);
	free(str);
}

char const *conf_get_str(char const *name)
{
	return getenv(name);
}

long long conf_get_int(char const *name)
{
	char const *str = conf_get_str(name);
	if (! str) with_error(ENOENT, "No such envvar '%s'") return;
	char *end;
	long long value = strtoll(str, &end, 0);
	if (*end != '\0') push_error(EINVAL, "bad integer '%s' for envvar '%s'", str, name);
	return value;
}

