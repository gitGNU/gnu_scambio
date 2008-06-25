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

static int header_ctor(struct header *h, char *msg)
{
	// h is already set to all 0
	memset(h->hash, -1, sizeof(h->hash));
	ssize_t parsed;
	while (*msg) {
		struct head_field *field = h->fields + h->nb_fields;
		parsed = parse_field(msg, &field->name, &field->value);
		if (parsed == -1) return -1;
		if (parsed == 0) break;	// end of message
		msg += parsed;
		// Hash these values
		unsigned hkey = header_key(field->name);
		field->hash_next = h->hash[hkey];
		h->hash[hkey] = h->nb_fields;
		if (++ h->nb_fields >= NB_MAX_FIELDS) {
			error("Too many fields in this header (max is "TOSTR(NB_MAX_FIELDS)")");
			return -1;
		}
	};
	return 0;
}

/*
 * Public Functions
 */

struct header *header_new(char *msg)
{
	struct header *h = calloc(1, sizeof(*h) + NB_MAX_FIELDS*sizeof(h->fields[0]));
	if (! h) return NULL;
	if (0 != header_ctor(h, msg)) {
		free(h);
		return NULL;
	}
	struct header *smaller_h = realloc(h, sizeof(*h) + h->nb_fields*sizeof(h->fields[0]));
	if (smaller_h) return smaller_h;
	warning("Cannot resize a struct header down to %d fields", h->nb_fields);
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
	if (0 != Write(fd, f->name, strlen(f->name))) return -1;
	if (0 != Write(fd, ": ", 2)) return -1;
	if (0 != Write(fd, f->value, strlen(f->value))) return -1;
	if (0 != Write(fd, "\n", 1)) return -1;
	return 0;
}

int header_write(struct header const *h, int fd)
{
	for (int f=0; f<h->nb_fields; f++) {
		int err;
		if (0 != (err = field_write(h->fields+f, fd))) return err;
	}
	return 0;
}

