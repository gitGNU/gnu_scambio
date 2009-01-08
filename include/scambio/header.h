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
#ifndef HEADER_H_080527
#define HEADER_H_080527

#include "scambio/queue.h"

/*
 * A header_field is a header entry.
 * It's composed of a name, and a value.
 * The value may itself have parameters. Let's see :
 *
 * name: main_value; some_param=glop; another_param=pasglop
 *
 * See ?
 * Parameters value may be surrounded by double quotes.
 * This is similar to internet message headers, but simplified :
 * - inlined (folded lines are unfolded) ;
 * - not restricted to 7bit ASCII ;
 * - all names are put to lowercase (so field names are case insensitive).
 * - comments (between parentheses) and new lines are removed.
 */
struct header_field {
	TAILQ_ENTRY(header_field) entry;
	char *name, *value;	// strdupped strings
};

struct header;
/* Create a new header field and add it to this header's fields.
 * given strings are strdupped.
 * Throws no error.
 */
struct header_field *header_field_new(struct header *, char const *name, char const *value);

/* Deletes a header field (removing it from the header it belongs to first).
 * Throws no error.
 */
void header_field_del(struct header_field *, struct header *);

/* Replace the name and/or value of a header_field (if name or value are NULL
 * the previous string is kept).
 */
void header_field_set(struct header_field *, char const *name, char const *value);

/*
 * These are some helper function to deal with parameters.
 */

/* Sets param_value to the beginning of this parameter's value, or to NULL if no such
 * parameter is found.
 * Returns the length of the value (if *param_value != NULL).
 * Throws no error.
 */
size_t parameter_find(char const *str, char const *param_name, char const **param_value);

/* Extract a parameter's value from a string and return it as a new allocated string.
 * Returns NULL if no such parameter is found.
 * Throws no error.
 */
char *parameter_extract(char const *str, char const *param_name);

/* If a value have some parameters, you may want a copy of the value without these parameters.
 * This gives you one, allocated.
 * Throws no error.
 */
char *parameter_suppress(char const *str);

/*
 * A header is merely an unordered set of header_fields.
 * As no header is supposed to have many fields, these are just assembled in a list.
 * Also, headers are ref counted.
 */
struct header {
	TAILQ_HEAD(header_fields, header_field) fields;
	int count;
};

/* Build an empty header.
 * The caller is the only reference owner.
 */
struct header *header_new(void);

/* Deletes a header.
 * Never call this but use header_unref() instead.
 * Throws no error but asserts that the count ref is 0.
 */
void header_del(struct header *);

/* Returns a new reference to the header.
 * Throws no error.
 */
static inline struct header *header_ref(struct header *h)
{
	h->count++;
	return h;
}

/* Unreference the header.
 * Throws no error.
 */
static inline void header_unref(struct header *h)
{
	if (--h->count <= 0) header_del(h);
}

/* Given a field name, returns a header_field with this name.
 * If previous is not NULL, will return the first matching. If previous
 * is set, will return the next field with this name.
 * If no (more) fields have this name, return NULL.
 * Throws no error.
 */
struct header_field *header_find(struct header const *, char const *name, struct header_field *prev);

/* Parse the given message header in text format until a blank line, and add all
 * fields to the given header.
 * Returns the number of chars consumed (not including the blank line).
 */
size_t header_parse(struct header *, char const *msg);

/* Write a header onto a filedescr.
 * FIXME: file is written to on error.
 */
void header_write(struct header const *, int fd);

/* Read a header from a filedescr (until blank line or EOF)
 */
void header_read(struct header *, int fd);

/* Similar to header_write(), but use a filename instead of a file descriptor.
 */
void header_to_file(struct header *, char const *filename);

/* Similar to header_read(), but use a filename instead of a file descriptor.
 * Also, it creates the header instead of merely adding new fields to an existing header.
 */
struct header *header_from_file(char const *filename);

#include "varbuf.h"
/* Write a header onto a variable buffer.
 */
void header_dump(struct header const *, struct varbuf *vb);

/* Display the header in the debug log.
 * Throws no error.
 */
void header_debug(struct header *);

/* Compute a digest for a header, and write it to buffer which should be big enought
 * (see digest.h for size constraints).
 */
#include "digest.h"
void header_digest(struct header *, size_t, char *buffer);

/*
 * Headers may have a type when used by Scambio, to keep track of the initial intend of
 * the message. Anyway, tools are encouraged to use "duck typing" when possible instead
 * of rigidly check this type.
 * Some predefined types are listed below.
 */

// Common values for type field (SC_TYPE_FIELD)
#define SC_DIR_TYPE      "dir"
#define SC_MAIL_TYPE     "mail"
#define SC_CAL_TYPE      "cal"
#define SC_IM_TYPE       "im"
#define SC_FILE_TYPE     "file"
#define SC_CONTACT_TYPE  "contact"
#define SC_BOOKMARK_TYPE "bookmark"
#define SC_MARK_TYPE     "mark"
#define SC_PERM_TYPE     "perms"

/* Tells wether this header has a specific type.
 */
bool header_has_type(struct header *, char const *type);

/* Specifically, is it a directory (as used by Scambio for its message directory tree) ?
 */
bool header_is_directory(struct header *);

/* Some headers are used by Scambio to reference other headers (notably, to delete
 * a header we add a new header which targets the header to be deleted).
 * This function returns the header targeted (referenced) by the given one,
 * or throws an error.
 */
#include "scambio/mdir.h"
mdir_version header_target(struct header *);

/*
 * Some "well known" header field names used by Scambio.
 */

#define SC_TYPE_FIELD        "sc-type"
#define SC_TARGET_FIELD      "sc-target"
#define SC_DIGEST_FIELD      "sc-digest"
#define SC_DIRID_FIELD       "sc-dirId"
#define SC_NAME_FIELD        "sc-name"
#define SC_FROM_FIELD        "sc-from"
#define SC_TO_FIELD          "sc-to"
#define SC_DESCR_FIELD       "sc-descr"
#define SC_EXTID_FIELD       "sc-extid"
#define SC_RESOURCE_FIELD    "sc-resource"
#define SC_START_FIELD       "sc-start"
#define SC_STOP_FIELD        "sc-stop"
#define SC_PERIOD_FIELD      "sc-period"
#define SC_LOCALID_FIELD     "sc-localid"
#define SC_STATUS_FIELD      "sc-status"
#define SC_URL_FIELD         "sc-url"
#define SC_HAVE_READ_FIELD   "sc-read"
#define SC_ALIAS_FIELD       "sc-alias"
#define SC_ALLOW_WRITE_FIELD "sc-allow-write"
#define SC_ALLOW_READ_FIELD  "sc-allow-read"
#define SC_ALLOW_ADMIN_FIELD "sc-allow-admin"
#define SC_DENY_WRITE_FIELD  "sc-deny-write"
#define SC_DENY_READ_FIELD   "sc-deny-read"
#define SC_DENY_ADMIN_FIELD  "sc-deny-admin"

#endif
