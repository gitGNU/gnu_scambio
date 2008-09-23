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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "scambio.h"
#include "scambio/error.h"

static struct error {
	int code;
	char str[1024];
} err_stack[64];
static unsigned nb_errors = 0;
static unsigned nb_acks = 1;
static unsigned ack_stack[16] = { 0 };

static unsigned nb_expected_errors(void)
{
	return ack_stack[nb_acks-1];
}

bool is_error(void)
{
	return nb_errors > nb_expected_errors();
}

int error_code(void)
{
	assert(nb_errors);
	return err_stack[nb_errors-1].code;
}

char const *error_str(void)
{
	assert(nb_errors);
	return err_stack[nb_errors-1].str;
}

void error_push(int code, char *fmt, ...)
{
	assert(nb_errors < sizeof_array(err_stack));
	struct error *const err = err_stack + nb_errors++;
	err->code = code;
	int len = 0;
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		len = vsnprintf(err->str, sizeof(err->str), fmt, ap);
		if (len >= (int)sizeof(err->str)) len = sizeof(err->str)-1;
		va_end(ap);
	}
	if (code) snprintf(err->str+len, sizeof(err->str)-len, "%s%s", len > 0 ? " : ":"", strerror(code));
	error1(err->str);
}

void error_save(void)
{
	assert(nb_acks < sizeof_array(ack_stack));
	ack_stack[nb_acks++] = nb_errors;
}

void error_restore(void)
{
	error_clear();
	assert(nb_acks > 0);
	nb_acks--;
}
void error_clear(void)
{
	assert(nb_errors >= nb_expected_errors());
	nb_errors = nb_expected_errors();
}


