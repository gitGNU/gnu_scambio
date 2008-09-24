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
#include <pth.h>
#include "scambio.h"
#include "scambio/error.h"

struct errbuf {
	unsigned nb_errors;
	unsigned nb_acks;
	unsigned ack_stack[16];
	struct error {
		int code;
		char str[1024];
	} err_stack[64];
};

static pth_key_t err_key;

void delete_errbuf(void *eb)
{
	debug("freeing errbuf @%p", eb);
	free(eb);
}

void error_begin(void)
{
	pth_key_create(&err_key, delete_errbuf);
}
void error_end(void)
{
	pth_key_delete(err_key);
}

static struct errbuf *errbuf_new(void)
{
	struct errbuf *eb = malloc(sizeof(*eb));
	debug("new errbuf @%p", eb);
	assert(eb);	// we cannot deal with this error
	eb->nb_errors = 0;
	eb->nb_acks = 1;
	eb->ack_stack[0] = 0;
	return eb;
}

static struct errbuf *get_errbuf(void)
{
	struct errbuf *eb = pth_key_getdata(err_key);
	if (! eb) {	// not already set
		eb = errbuf_new();
		pth_key_setdata(err_key, eb);
	}
	return eb;
}

static unsigned nb_expected_errors(void)
{
	struct errbuf *eb = get_errbuf();
	return eb->ack_stack[eb->nb_acks-1];
}

bool is_error(void)
{
	struct errbuf *eb = get_errbuf();
	return eb->nb_errors > nb_expected_errors();
}

int error_code(void)
{
	struct errbuf *eb = get_errbuf();
	assert(eb->nb_errors);
	return eb->err_stack[eb->nb_errors-1].code;
}

char const *error_str(void)
{
	struct errbuf *eb = get_errbuf();
	assert(eb->nb_errors);
	return eb->err_stack[eb->nb_errors-1].str;
}

void error_push(int code, char *fmt, ...)
{
	struct errbuf *eb = get_errbuf();
	assert(eb->nb_errors < sizeof_array(eb->err_stack));
	struct error *const err = eb->err_stack + eb->nb_errors++;
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
	struct errbuf *eb = get_errbuf();
	assert(eb->nb_acks < sizeof_array(eb->ack_stack));
	eb->ack_stack[eb->nb_acks++] = eb->nb_errors;
}

void error_restore(void)
{
	struct errbuf *eb = get_errbuf();
	error_clear();
	assert(eb->nb_acks > 0);
	eb->nb_acks--;
}
void error_clear(void)
{
	struct errbuf *eb = get_errbuf();
	assert(eb->nb_errors >= nb_expected_errors());
	eb->nb_errors = nb_expected_errors();
}

