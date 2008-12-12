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

/* This structure represents a message header, ie a map from
 * field names to field values.
 * These strings and taken from the original message, which is patched
 * (addition of terminating zeros, removal of comments and new lines...).
 * So thoses char pointers are valid untill the received message is.
 * These strings are UTF-8 but we do not really care here.
 *
 * To speed up the search for a name's value, we use a small hash.
 */
struct header {
	unsigned nb_fields;
	int count;
	// Variable size
	struct head_field {
		char *name, *value;	// strdupped strings
	} fields[];
};

/* Build an empty header.
 */
struct header *header_new(void);

void header_del(struct header *h);

static inline struct header *header_ref(struct header *header)
{
	header->count++;
	return header;
}

static inline void header_unref(struct header *header)
{
	if (--header->count <= 0) header_del(header);
}

// Given a field name, return the (first) field value
// or NULL if undefined.
char const *header_search(struct header const *h, char const *name);

// Given a field name, return all the values with given field name
// whatever *nb, the returned pointer must be freed
char const **header_search_all(struct header const *h, char const *name, unsigned *nb);

size_t header_parse(struct header *h, char const *msg);

// Write a header onto a filedescr
// FIXME: file is written to on error
void header_write(struct header const *h, int fd);

// Read a header from a filedescr (until blank line)
void header_read(struct header *h, int fd);

#include "varbuf.h"
// Write a header onto a variable buffer
void header_dump(struct header const *h, struct varbuf *vb);
void header_debug(struct header *h);

void header_add_field(struct header *h, char const *name, char const *value);

// Return a pointer to the beginning of the value.
// Return the length of the value if *value!=NULL.
size_t header_find_parameter(char const *name, char const *field_value, char const **value);
// Same result. Error may be EMSGSIZE or ENOENT, in which case value is set to "".
// value will always be nul terminated
void header_copy_parameter(char const *name, char const *field_value, size_t max_len, char *value);

// If a value have some parameters, you may want a copy of the value without these parameters
size_t header_stripped_value(char const *field_value, size_t max_len, char *value);

#include "digest.h"
void header_digest(struct header *h, size_t, char *buffer);
struct header *header_from_file(char const *filename);
void header_to_file(struct header *h, char const *filename);
bool header_has_type(struct header *h, char const *type);
bool header_is_directory(struct header *h);
#include "scambio/mdir.h"
mdir_version header_target(struct header *h);

#define SC_TYPE_FIELD     "sc-type"
#define SC_TARGET_FIELD   "sc-target"
#define SC_DIGEST_FIELD   "sc-digest"
#define SC_DIRID_FIELD    "sc-dirId"
#define SC_NAME_FIELD     "sc-name"
#define SC_FROM_FIELD     "sc-from"
#define SC_TO_FIELD       "sc-to"
#define SC_DESCR_FIELD    "sc-descr"
#define SC_SENT_DATE      "sc-sent-at"
#define SC_EXTID_FIELD    "sc-extid"
#define SC_RESOURCE_FIELD "sc-resource"
#define SC_START_FIELD    "sc-start"
#define SC_STOP_FIELD     "sc-stop"
#define SC_PERIOD_FIELD   "sc-period"
#define SC_LOCALID_FIELD  "sc-localid"
#define SC_STATUS_FIELD   "sc-status"

// Common values for type field
#define SC_DIR_TYPE       "dir"
#define SC_MAIL_TYPE      "mail"
#define SC_CAL_TYPE       "cal"
#define SC_IM_TYPE        "im"
#define SC_DIR_TYPE       "dir"
#define SC_FILE_TYPE      "file"
#define SC_CONTACT_TYPE   "contact"

#endif
