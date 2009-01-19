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
#include <strings.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "scambio.h"
#include "config.h"
#include "scambio/header.h"
#include "log.h"
#include "misc.h"
#include "varbuf.h"

/*
 * Header Fields
 */

static void str_tolower(char *str)
{
	while (*str != '\0') {
		*str = tolower(*str);
		str++;
	}
}

// Throws no error
static void header_field_ctor(struct header_field *hf, struct header *h, char const *name, char const *value)
{
	hf->name = Strdup(name);
	hf->value = Strdup(value);
	str_tolower(hf->name);
	TAILQ_INSERT_TAIL(&h->fields, hf, entry);
}

struct header_field *header_field_new(struct header *h, char const *name, char const *value)
{
	struct header_field *hf = Malloc(sizeof(*hf));
	header_field_ctor(hf, h, name, value);
	return hf;
}

static void header_field_dtor(struct header_field *hf, struct header *h)
{
	FreeIfSet(&hf->name);
	FreeIfSet(&hf->value);
	TAILQ_REMOVE(&h->fields, hf, entry);
}

void header_field_del(struct header_field *hf, struct header *h)
{
	header_field_dtor(hf, h);
	free(hf);
}

void header_field_set(struct header_field *hf, char const *name, char const *value)
{
	if (name) {
		char *new_name = Strdup(name);
		on_error return;
		FreeIfSet(&hf->name);
		hf->name = new_name;
		str_tolower(hf->name);
	}
	if (value) {
		char *new_value = Strdup(value);
		on_error return;
		FreeIfSet(&hf->value);
		hf->value = new_value;
	}
}

/*
 * Parameters
 */

char const *find_next_param_delim(char const *str)
{
	bool quoted = false;
	while (*str) {
		if (*str == '"') {
			quoted = !quoted;
		} else if (! quoted && *str == ';') {
			break;
		}
		str++;
	}
	return str;
}

size_t parameter_find(char const *str, char const *param_name, char const **param_value)
{
	size_t const len = strlen(param_name);
	size_t ret = 0;
	*param_value = NULL;
	do {
		// First find next separator
		str = find_next_param_delim(str);
		if (! *str) return 0;
		str++;	// skip delimiter
		// Then skip the only white space that may lie here
		while (*str == ' ') str++;	// there should be only one
		if (0 == strncasecmp(param_name, str, len)) {	// found
			str += len;
			while (*str == ' ') str++;
			if (*str != '=') continue;
			str++;
			while (*str == ' ') str++;
			char delim = ';';
			if (*str == '"') {
				str++;
				delim = '"';
			}
			*param_value = str;
			while (*str != '\0' && *str != delim) {
				ret++;
				str++;
			}
			return ret;
		}
	} while (*str);
	return 0;
}

char *parameter_extract(char const *str, char const *param_name)
{
	char const *param_value;
	size_t len = parameter_find(str, param_name, &param_value);
	if (! param_value) return NULL;
	char *res = Malloc(len + 1);
	memcpy(res, param_value, len);
	res[len] = '\0';
	return res;
}

char *parameter_suppress(char const *str)
{
	char const *v = find_next_param_delim(str);
	while (v > str && isspace(*(v-1))) v--;
	char *res = Malloc(v - str + 1);
	memcpy(res, str, v - str);
	res[v - str] = '\0';
	return res;
}

/*
 * Headers
 */

static void header_ctor(struct header *h)
{
	debug("header @%p", h);
	TAILQ_INIT(&h->fields);
	h->count = 1;
}

struct header *header_new(void)
{
	struct header *h = Malloc(sizeof(*h));
	header_ctor(h);
	return h;
}

static void header_dtor(struct header *h)
{
	debug("header @%p", h);
	assert(h->count <= 0);
	struct header_field *hf;
	while (NULL != (hf = TAILQ_FIRST(&h->fields))) {
		header_field_del(hf, h);
	}
}

void header_del(struct header *h)
{
	header_dtor(h);
	free(h);
}

extern inline struct header *header_ref(struct header *);
extern inline void header_unref(struct header *);

struct header_field *header_find(struct header const *h, char const *name, struct header_field *prev)
{
	debug("looking for %s in header @%p", name, h);
	for (
		struct header_field *hf = prev ? TAILQ_NEXT(prev, entry) : TAILQ_FIRST(&h->fields);
		hf != NULL;
		hf = TAILQ_NEXT(hf, entry)
	) {
		if (0 == strcasecmp(hf->name, name)) {
			debug("  found, value = %s", hf->value);
			return hf;
		}
	}
	return NULL;
}

/* Reads a string until given delimiter, and compact it inplace
 * (ie. remove comments, new lines and useless spaces).
 * Returns the number of parsed chars
 */
static ssize_t parse(char const *msg, char **ptr, bool (*is_delimiter)(char const *))
{
	char const *src = msg;
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, 1000, true)) return 0;
	bool rem_space = true;
	while (! is_delimiter(src)) {
		if (*src == '\0') {
			error_push(0, "Unterminated string in header : '%s'", msg);
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
	}
	on_error {
		error_save();
		varbuf_dtor(&vb);
		*ptr = NULL;
		error_restore();
	} else {
		*ptr = varbuf_unbox(&vb);
	}
	return src - msg;
}

static bool iseol(char c)
{
	return c == '\n' || c == '\r';
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
	if (parsedV == 0) with_error(0, "Field '%s' has no value", *name) return 0;
	parsedV++;	// skip delimiter
	return parsedN + parsedV;
}

size_t header_parse(struct header *h, char const *msg_)
{
	ssize_t parsed;
	char const *msg = msg_;
	while (*msg) {
		char *field_name, *field_value;
		if_fail (parsed = parse_field(msg, &field_name, &field_value)) break;
		if (parsed == 0) break;	// end of message
		debug("parsed field '%s' to value '%s'", field_name, field_value);
		(void)header_field_new(h, field_name, field_value);
		free(field_name);
		free(field_value);
		on_error break;
		msg += parsed;
	};
	return msg - msg_;
}

static void field_write(struct header_field const *hf, int fd)
{
	Write_strs(fd, hf->name, ": ", hf->value, "\n", NULL);
}

void header_write(struct header const *h, int fd)
{
	debug("writing to fd %d", fd);
	struct header_field *hf;
	TAILQ_FOREACH(hf, &h->fields, entry) {
		field_write(hf, fd);
	}
	Write(fd, "\n", 1);
}

void header_read(struct header *h, int fd)
{
	debug("reading from fd %d", fd);
	int nb_lines = 0;
	struct varbuf vb;
	char *line;
	bool eoh_reached = false;
	if_fail (varbuf_ctor(&vb, 10240, true)) return;
	error_save();
	while (1) {
		if_fail (varbuf_read_line(&vb, fd, MAX_HEADLINE_LENGTH, &line)) break;
		if (++ nb_lines > MAX_HEADER_LINES) with_error(0, "Too many lines in header") break;
		if (line_match(line, "")) {
			debug("end of headers");
			// forget this line
			varbuf_cut(&vb, line);
			eoh_reached = true;
			break;
		}
	}
	if (is_error() && error_code() == ENOENT) {
		debug("BTW, its EOF");
		eoh_reached = true;
	}
	error_restore();
	if (nb_lines == 0) error_push(0, "No lines in header");
	if (! eoh_reached) error_push(0, "End of file before end of header");
	if (! is_error()) (void)header_parse(h, vb.buf);
	varbuf_dtor(&vb);
}

void header_to_file(struct header *h, char const *filename)
{
	if_fail (Mkdir_for_file(filename)) return;
	int fd = open(filename, O_WRONLY|O_CREAT, 0640);
	if (fd < 0) with_error(errno, "open(%s)", filename) return;
	header_write(h, fd);
	(void)close(fd);
}

struct header *header_from_file(char const *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) with_error(errno, "open(%s)", filename) return NULL;
	struct header *h;
	do {
		if_fail (h = header_new()) {
			h = NULL;
			break;
		}
		if_fail (header_read(h, fd)) {
			header_unref(h);
			h = NULL;
			break;
		}
	} while (0);
	close(fd);
	return h;
}

static void field_dump(struct header_field const *hf, struct varbuf *vb)
{
	varbuf_append_strs(vb, hf->name, ": ", hf->value, "\n", NULL);
}

void header_dump(struct header const *h, struct varbuf *vb)
{
	struct header_field *hf;
	TAILQ_FOREACH(hf, &h->fields, entry) {
		field_dump(hf, vb);
	}
	varbuf_append(vb, 1, "\n");
}

void header_debug(struct header *h)
{
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, 1000, true)) {
		error_clear();
		return;
	}
	header_dump(h, &vb);
	if (! is_error()) debug("header :\n%s", vb.buf);
	varbuf_dtor(&vb);
	error_clear();
}

void header_digest(struct header *h, size_t size, char *buffer)
{
	(void)size;	// FIXME: not me but the digest interface
	struct varbuf vb;
	varbuf_ctor(&vb, 5000, true);
	on_error return;
	do {
		header_dump(h, &vb);
		on_error break;
		size_t dig_len = digest(buffer, vb.used, vb.buf);
		buffer[dig_len] = '\0';
	} while (0);
	varbuf_dtor(&vb);
}

bool header_has_type(struct header *h, char const *type)
{
	struct header_field *hf = header_find(h, SC_TYPE_FIELD, NULL);
	return hf && 0==strcmp(hf->value, type);
}

bool header_is_directory(struct header *h)
{
	return header_has_type(h, SC_DIR_TYPE);
}

mdir_version header_target(struct header *h)
{
	struct header_field *hf = header_find(h, SC_TARGET_FIELD, NULL);
	if (! hf) with_error(0, "Header lacks a target") return 0;
	return mdir_str2version(hf->value);
}

