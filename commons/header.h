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
	int nb_fields;
#	define FIELD_HASH_SIZE 32
	int hash[FIELD_HASH_SIZE];	// first field with this hash value;
	char *end;	// end of header in initial message
	// Variable size
	struct head_field {
		char *name, *value;
		int hash_next;	// next field with same hash value, or -1 if last.
	} fields[];
};

/* Build a header from this message.
 * Do not free the message while the header is valid, for it keeps some pointer to it.
 * Returns NULL on error.
 * In any case, the message is modified in undefined way until header->end
 */
struct header *header_new(char *msg);

void header_del(struct header *);

// Given a name, returns the hash key to speed up search
unsigned header_key(char const *name);

// Given a field name and its key, return the field value
// or NULL if undefined.
char const *header_search(struct header const *h, char const *name, unsigned key);

// Write a header onto a filedescr
// Beware : file is written to on error
int header_write(struct header const *h, int fd);

// Beware : name and value must lasts (as long as header do)
int header_add_field(struct header *h, char const *name, unsigned key, char const *value);

// Return a pointer to the beginning of the value.
// Return -ENOENT if not found, or the length of the value if *value!=NULL.
int header_find_parameter(char const *name, char const *field_value, char const **value);
// Same result. Error may be -EMSGSIZE or -ENOENT
int header_copy_parameter(char const *name, char const *field_value, size_t max_len, char *value);

#endif