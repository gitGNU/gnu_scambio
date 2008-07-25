#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include "scambio.h"
#include "config.h"
#include "header.h"
#include "log.h"
#include "misc.h"

/*
 * Data Definitions
 */

#define NB_MAX_FIELDS 5000

/*
 * Private Functions
 */

static bool iseol(char c)
{
	return c == '\n' || c == '\r';
}

// Read a value until given delimiter, and compact it inplace (remove comments, new lines and useless spaces).
static ssize_t parse(char *msg, char **ptr, bool (*is_delimiter)(char const *))
{
	char *dst, *src;
	*ptr = dst = src = msg;
	bool rem_space = true;
	while (! is_delimiter(src)) {
		if (*src == '\0') {
			error("Unterminated string in header : '%s'", msg);
			return -1;	// we are supposed to reach the delimiter first
		}
		if (isspace(*src)) {
			*src = ' ';	// simplify by using only one kind of space char
			if (rem_space) {
				// just ignore this char
			} else {
				rem_space = true;
				if (dst != src) *dst = *src;
				dst++;
			}
		} else {
			rem_space = false;
			if (dst != src) *dst = *src;
			dst++;
		}
		src++;
	}
	// Trim the tail of the value
	while (dst > msg && isspace(dst[-1])) dst--;
	// Zero terminate it, may overwrite the delimiter
	*dst = '\0';
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

static ssize_t parse_field(char *msg, char **name, char **value)
{
	ssize_t parsedN = parse(msg, name, is_end_of_name);
	if (parsedN == -1) return -1;
	if (parsedN == 0) return 0;
	parsedN++;	// skip delimiter
	ssize_t parsedV = parse(msg + parsedN, value, is_end_of_value);
	if (parsedV <= 0) return -1;
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

static int header_ctor(struct header *h, char *msg)
{
	// h is already set to all 0
	memset(h->hash, -1, sizeof(h->hash));
	ssize_t parsed;
	while (*msg) {
		if (h->nb_fields >= NB_MAX_FIELDS) {
			error("Too many fields in this header (max is "TOSTR(NB_MAX_FIELDS)")");
			return -E2BIG;
		}
		struct head_field *field = h->fields + h->nb_fields;
		parsed = parse_field(msg, &field->name, &field->value);
		if (parsed == -1) return -1;
		if (parsed == 0) break;	// end of message
		msg += parsed;
		// Lowercase inplace stored field names
		str_tolower(field->name);
		// Hash these values
		unsigned hkey = header_key(field->name);
		field->hash_next = h->hash[hkey];
		h->hash[hkey] = h->nb_fields ++;
	};
	h->end = msg;
	return 0;
}

/*
 * Public Functions
 */

struct header *header_new(char *msg)
{
	debug("header_new(msg='%s')", msg);
	struct header *h = calloc(1, sizeof(*h) + NB_MAX_FIELDS*sizeof(h->fields[0]));
	if (! h) return NULL;
	if (0 != header_ctor(h, msg)) {
		free(h);
		return NULL;
	}
	return h;
}

void header_del(struct header *h)
{
	free(h);
}

unsigned header_key(char const *str)
{
	size_t max_len = 25;
	unsigned hkey = 0;
	while (*str != '\0' && max_len--) hkey += *str;
	return hkey % FIELD_HASH_SIZE;
}

char const *header_search(struct header const *h, char const *name, unsigned key)
{
	int f = h->hash[key];
	while (f != -1) {
		assert(f < h->nb_fields);
		if (0 == strcmp(h->fields[f].name, name)) {
			return h->fields[f].value;
		}
		f = h->fields[f].hash_next;
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
	for (int f=0; f<h->nb_fields; f++) {
		int err;
		if (0 != (err = field_write(h->fields+f, fd))) return err;
	}
	return 0;
}

int header_dump(struct header const *h, struct varbuf *vb)
{
	varbuf_clean(vb);
	for (int f=0; f<h->nb_fields; f++) {
		int err;
		if (0 != (err = field_dump(h->fields+f, vb))) return err;
	}
	return 0;
}

int header_add_field(struct header *h, char const *name, unsigned key, char const *value)
{
	if (h->nb_fields >= NB_MAX_FIELDS) return -E2BIG;
	struct head_field *field = h->fields + h->nb_fields;
	field->name = (char *)name;	// won't be written to from now on
	field->value = (char *)value;
	field->hash_next = h->hash[key];
	h->hash[key] = h->nb_fields ++;
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

