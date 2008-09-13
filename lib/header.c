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
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include "scambio.h"
#include "config.h"
#include "header.h"
#include "log.h"
#include "misc.h"

/*
 * Data Definitions
 */

#define NB_MAX_FIELDS 5000	// FIXME

/*
 * Private Functions
 */

static bool iseol(char c)
{
	return c == '\n' || c == '\r';
}

// Read a value until given delimiter, and compact it inplace (remove comments, new lines and useless spaces). Returns the number of parsed chars
static ssize_t parse(char const *msg, char **ptr, bool (*is_delimiter)(char const *))
{
	char const *src = msg;
	struct varbuf vb;
	varbuf_ctor(&vb, 1000, true);
	on_error return 0;
	bool rem_space = true;
	while (! is_delimiter(src)) {
		if (*src == '\0') {
			error_push(EINVAL, "Unterminated string in header : '%s'", msg);
			break;
		}
		if (isspace(*src)) {
			if (rem_space) {
				// just ignore this char
			} else {
				rem_space = true;
				varbuf_append(&vb, 1, " ");
			}
		} else {
			rem_space = false;
			varbuf_append(&vb, 1, src);
		}
		src++;
	}
	if (! is_error()) {
		// Trim the tail of the value
		while (vb.used && isspace(vb.buf[vb.used-1])) varbuf_chop(&vb, 1);
		varbuf_stringifies(&vb);
	}
	on_error {
		error_ack();
		varbuf_dtor(&vb);
		*ptr = NULL;
		error_clear();
	} else {
		*ptr = varbuf_unbox(&vb);
	}
	return src - msg;
}

static bool is_end_of_name(char const *msg)
{
	return *msg == ':' || iseol(*msg);
}

static bool is_end_of_value(char const *msg)
{
	return iseol(msg[0]) && !isblank(msg[1]);
}

static ssize_t parse_field(char const *msg, char **name, char **value)
{
	ssize_t parsedN = parse(msg, name, is_end_of_name);
	on_error return 0;
	if (parsedN == 0) return 0;
	parsedN++;	// skip delimiter
	ssize_t parsedV = parse(msg + parsedN, value, is_end_of_value);
	on_error {
		free(*name);
		return 0;
	}
	if (parsedV == 0) with_error(EINVAL, "Field '%s' has no value", *name) return 0;
	parsedV++;	// skip delimiter
	return parsedN + parsedV;
}

static void str_tolower(char *str)
{
	while (*str != '\0') {
		*str = tolower(*str);
		str++;
	}
}

static void header_ctor(struct header *h)
{
	h->nb_fields = 0;
}

static void header_dtor(struct header *h)
{
	while (h->nb_fields--) {
		struct head_field *f = h->fields+h->nb_fields;
		free(f->name);
		free(f->value);
	}
}

/*
 * Public Functions
 */

struct header *header_new(void)
{
	struct header *h = malloc(sizeof(*h) + NB_MAX_FIELDS*sizeof(h->fields[0]));
	if (! h) with_error(errno, "Cannot malloc header") return NULL;
	header_ctor(h);
	on_error {
		free(h);
		return NULL;
	}
	return h;
}

void header_del(struct header *h)
{
	header_dtor(h);
	free(h);
}

char const *header_search(struct header const *h, char const *name)
{
	for (unsigned f=0; f<h->nb_fields; f++) {
		if (0 == strcmp(h->fields[f].name, name)) {
			return h->fields[f].value;
		}
	}
	return NULL;
}

static void field_write(struct head_field const *f, int fd)
{
	Write(fd, f->name, strlen(f->name));
	Write(fd, ": ", 2);
	Write(fd, f->value, strlen(f->value));
	Write(fd, "\n", 1);
}

static void field_dump(struct head_field const *f, struct varbuf *vb)
{
	varbuf_append(vb, strlen(f->name), f->name);
	varbuf_append(vb, 2, ": ");
	varbuf_append(vb, strlen(f->value), f->value);
	varbuf_append(vb, 1, "\n");
}

void header_write(struct header const *h, int fd)
{
	for (unsigned f=0; f<h->nb_fields; f++) {
		field_write(h->fields+f, fd);
	}
	Write(fd, "\n", 1);
}

void header_parse(struct header *h, char const *msg) {
	debug("msg = '%s'", msg);
	ssize_t parsed;
	while (*msg) {
		if (h->nb_fields >= NB_MAX_FIELDS) {
			error_push(E2BIG, "Too many fields in this header (max is "TOSTR(NB_MAX_FIELDS)")");
			return;
		}
		struct head_field *field = h->fields + h->nb_fields;
		parsed = parse_field(msg, &field->name, &field->value);
		on_error return;
		if (parsed == 0) break;	// end of message
		debug("parsed field '%s' to value '%s'", field->name, field->value);
		h->nb_fields ++;
		msg += parsed;
		// Lowercase stored field names
		str_tolower(field->name);
	};
}

void header_read(struct header *h, int fd)
{
	debug("reading from fd %d", fd);
	int nb_lines = 0;
	struct varbuf vb;
	char *line;
	bool eoh_reached = false;
	varbuf_ctor(&vb, 10240, true);
	on_error return;
	error_ack();
	while (1) {
		varbuf_read_line(&vb, fd, MAX_HEADLINE_LENGTH, &line);
		on_error break;
		if (++ nb_lines > MAX_HEADER_LINES) {
			error_push(E2BIG, "Too many lines in header");
			break;
		}
		debug("line is '%s'", line);
		if (line_match(line, "")) {
			debug("end of headers");
			// forget this line
			vb.used = line - vb.buf + 1;
			vb.buf[vb.used-1] = '\0';
			eoh_reached = true;
			break;
		}
	}
	if (error_code() == ENOENT) {
		debug("BTW, its EOF");
		eoh_reached = true;
	}
	error_clear();
	if (nb_lines == 0) error_push(EINVAL, "No lines in header");
	if (! eoh_reached) error_push(EINVAL, "End of file before end of header");
	if (! is_error()) header_parse(h, vb.buf);
	varbuf_dtor(&vb);
}

void header_dump(struct header const *h, struct varbuf *vb)
{
	varbuf_clean(vb);
	for (unsigned f=0; f<h->nb_fields; f++) {
		field_dump(h->fields+f, vb);
	}
	varbuf_stringifies(vb);	// so that empty headers leads to empty lines
}

void header_debug(struct header *h)
{
	struct varbuf vb;
	varbuf_ctor(&vb, 1000, true);
	on_error return;
	header_dump(h, &vb);
	if (! is_error()) debug("header : %s", vb.buf);
	varbuf_dtor(&vb);
}

void header_add_field(struct header *h, char const *name, char const *value)
{
	if (h->nb_fields >= NB_MAX_FIELDS) with_error(E2BIG, "Too many fields in header") return;
	struct head_field *field = h->fields + h->nb_fields;
	field->name = strdup(name);	// won't be written to from now on
	if (! field->name) with_error(ENOMEM, "Cannot strdup field name") return;
	str_tolower(field->name);
	field->value = strdup(value);
	if (! field->value) {
		free(field->name);
		with_error(ENOMEM, "Cannot strdup field value") return;
	}
}

size_t header_find_parameter(char const *name, char const *field_value, char const **value)
{
	char const *v = field_value;
	size_t len = strlen(name);
	size_t ret = 0;
	*value = NULL;
	do {
		// First find next separator
		for ( ; *v != ';'; v++) {
			if (*v == '\0') with_error(ENOENT, "No parameters in field value") return 0;
		}
		// Then skip the only white space that may lie here
		while (*v == ' ') v++;	// there should be only one
		if (0 == strncasecmp(name, v, len)) {	// found
			v += len;
			while (*v == ' ') v++;
			if (*v != '=') continue;
			v++;
			while (*v == ' ') v++;
			char delim = ';';
			if (*v == '"') {
				v++;
				delim = '"';
			}
			*value = v;
			while (*v && *v != delim) ret++;
			return ret;
		}
	} while (*v != '\0');
	with_error(ENOENT, "No such parameter '%s'", name) return 0;
}

void header_copy_parameter(char const *name, char const *field_value, size_t max_len, char *value)
{
	char const *str;
	int str_len = header_find_parameter(name, field_value, &str);
	on_error return;
	if (str_len >= (int)max_len) with_error(EMSGSIZE, "parameter length is %d while buf size is %zu", str_len, max_len) return;
	memcpy(value, str, str_len);
	value[str_len] = '\0';
}

void header_digest(struct header *header, size_t size, char *buffer)
{
	(void)size;	// FIXME: not me but the digest interface
	struct varbuf vb;
	varbuf_ctor(&vb, 5000, true);
	on_error return;
	do {
		header_dump(header, &vb);
		on_error break;
		size_t dig_len = digest(buffer, vb.used, vb.buf);
		buffer[dig_len] = '\0';
	} while (0);
	varbuf_dtor(&vb);
}
