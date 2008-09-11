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

// Read a value until given delimiter, and compact it inplace (remove comments, new lines and useless spaces).
static ssize_t parse(char const *msg, char **ptr, bool (*is_delimiter)(char const *))
{
	int err = 0;
	char const *src = msg;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 1000, true))) return err;
	bool rem_space = true;
	while (! is_delimiter(src)) {
		if (*src == '\0') {
			error("Unterminated string in header : '%s'", msg);
			err = -1;	// FIXME
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
	if (! err) {
		// Trim the tail of the value
		while (vb.used && isspace(vb.buf[vb.used-1])) varbuf_chop(&vb, 1);
		err = varbuf_stringifies(&vb);
	}
	if (err) {
		varbuf_dtor(&vb);
		*ptr = NULL;
		return err;
	}
	*ptr = varbuf_unbox(&vb);
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
	if (parsedN < 0) return parsedN;
	if (parsedN == 0) return 0;
	parsedN++;	// skip delimiter
	ssize_t parsedV = parse(msg + parsedN, value, is_end_of_value);
	if (parsedV == 0) {
		error("Field '%s' has no value", *name);
		parsedV = -EINVAL;
	}
	if (parsedV < 0) {
		free(*name);
		return parsedV;
	}
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

static int header_ctor(struct header *h)
{
	h->nb_fields = 0;
	return 0;
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

int header_new(struct header **h)
{
	int err = 0;
	assert(h);
	*h = malloc(sizeof(**h) + NB_MAX_FIELDS*sizeof((*h)->fields[0]));
	if (! *h) return -ENOMEM;
	if (0 != (err = header_ctor(*h))) {
		free(*h);
	}
	return err;
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

static int field_write(struct head_field const *f, int fd)
{
	int err = 0;
	if (0 != (err = Write(fd, f->name, strlen(f->name)))) return err;
	if (0 != (err = Write(fd, ": ", 2))) return err;
	if (0 != (err = Write(fd, f->value, strlen(f->value)))) return err;
	if (0 != (err = Write(fd, "\n", 1))) return err;
	return err;
}

static int field_dump(struct head_field const *f, struct varbuf *vb)
{
	int err = 0;
	if (0 != (err = varbuf_append(vb, strlen(f->name), f->name))) return err;
	if (0 != (err = varbuf_append(vb, 2, ": "))) return err;
	if (0 != (err = varbuf_append(vb, strlen(f->value), f->value))) return err;
	if (0 != (err = varbuf_append(vb, 1, "\n"))) return err;
	return err;
}

int header_write(struct header const *h, int fd)
{
	for (unsigned f=0; f<h->nb_fields; f++) {
		int err;
		if (0 != (err = field_write(h->fields+f, fd))) return err;
	}
	return Write(fd, "\n", 1);
}

int header_parse(struct header *h, char const *msg) {
	debug("msg = '%s'", msg);
	ssize_t parsed;
	while (*msg) {
		if (h->nb_fields >= NB_MAX_FIELDS) {
			error("Too many fields in this header (max is "TOSTR(NB_MAX_FIELDS)")");
			return -E2BIG;
		}
		struct head_field *field = h->fields + h->nb_fields;
		parsed = parse_field(msg, &field->name, &field->value);
		if (parsed < 0) return parsed;
		if (parsed == 0) break;	// end of message
		debug("parsed field '%s' to value '%s'", field->name, field->value);
		h->nb_fields ++;
		msg += parsed;
		// Lowercase stored field names
		str_tolower(field->name);
	};
	return 0;
}

int header_read(struct header *h, int fd)
{
	debug("reading from fd %d", fd);
	int err = 0;
	int nb_lines = 0;
	struct varbuf vb;
	char *line;
	bool eoh_reached = false;
	if (0 != (err = varbuf_ctor(&vb, 10240, true))) return err;
	while (0 == (err = varbuf_read_line(&vb, fd, MAX_HEADLINE_LENGTH, &line))) {
		if (++ nb_lines > MAX_HEADER_LINES) {
			err = -E2BIG;
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
	if (err == 1) {
		debug("BTW, its EOF");
		err = 0;	// no more use for EOF
		eoh_reached = true;
	}
	if (nb_lines == 0) err = -EINVAL;
	if (! eoh_reached) err = -EINVAL;
	if (! err) err = header_parse(h, vb.buf);
	varbuf_dtor(&vb);
	return err;
}

int header_dump(struct header const *h, struct varbuf *vb)
{
	varbuf_clean(vb);
	for (unsigned f=0; f<h->nb_fields; f++) {
		int err;
		if (0 != (err = field_dump(h->fields+f, vb))) return err;
	}
	varbuf_stringifies(vb);	// so that empty headers leads to empty lines
	return 0;
}

void header_debug(struct header *h)
{
	struct varbuf vb;
	if (0 != varbuf_ctor(&vb, 1000, true)) return;
	if (0 == header_dump(h, &vb)) debug("header : %s", vb.buf);
	varbuf_dtor(&vb);
}

int header_add_field(struct header *h, char const *name, char const *value)
{
	if (h->nb_fields >= NB_MAX_FIELDS) return -E2BIG;
	struct head_field *field = h->fields + h->nb_fields;
	field->name = strdup(name);	// won't be written to from now on
	if (! field->name) return -ENOMEM;
	str_tolower(field->name);
	field->value = strdup(value);
	if (! field->value) {
		free(field->name);
		return -ENOMEM;
	}
	return 0;
}

int header_find_parameter(char const *name, char const *field_value, char const **value)
{
	char const *v = field_value;
	size_t len = strlen(name);
	int ret = 0;
	*value = NULL;
	do {
		// First find next separator
		for ( ; *v != ';'; v++) {
			if (*v == '\0') return -ENOENT;
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
	return -ENOENT;
}

int header_copy_parameter(char const *name, char const *field_value, size_t max_len, char *value)
{
	char const *str;
	int str_len = header_find_parameter(name, field_value, &str);
	if (! str || ! str_len) return -ENOENT;
	if (str_len >= (int)max_len) return -EMSGSIZE;
	memcpy(value, str, str_len);
	value[str_len] = '\0';
	return str_len;
}

int header_digest(struct header *header, size_t size, char *buffer)
{
	(void)size;	// FIXME: not me but the digest interface
	int err = 0;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 5000, true))) return err;
	do {
		if (0 != (err = header_dump(header, &vb))) break;
		size_t dig_len = digest(buffer, vb.used, vb.buf);
		buffer[dig_len] = '\0';
	} while (0);
	varbuf_dtor(&vb);
	return err;
}
