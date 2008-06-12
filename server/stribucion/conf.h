#ifndef CONF_H_080529
#define CONF_H_080529

#include <stdbool.h>
#include <sys/types.h>
#include <regex.h>

struct conf_test {
	struct conf_condition {
		enum conf_op { OP_ALWAYS, OP_SET, OP_UNSET, OP_GT, OP_GE, OP_LT, OP_LE, OP_EQ, OP_RE } op;
		char *field_name;
		unsigned field_key;
		enum value_type { TYPE_STRING, TYPE_NUMBER, TYPE_DEREF } value_type;
		union conf_value {
			long long number;
			char *string;
			struct conf_deref {
				char *name;
				unsigned key;
			} deref;
		} value;
		bool re_match_set;
		regex_t re_match;
	} condition;
	struct conf_action {
		enum action_type { ACTION_DISCARD, ACTION_COPY, ACTION_MOVE } type;
		enum dest_type { DEST_STRING, DEST_DEREF } dest_type;
		union conf_dest {
			char *string;
			struct conf_deref deref;
		} dest;
	} action;
};

#define NB_MAX_TESTS 1000	// FIXME: resizeable

struct conf {
	unsigned nb_tests;
	struct conf_test tests[];	// Variable size
};

struct conf *conf_new(char const *path);
void conf_del(struct conf *);
void conf_dump(struct conf *, void (*printer)(char const *fmt, ...));
// Returns the length of actions
struct header;
unsigned conf_eval(struct conf *, struct header const *, struct conf_action const *actions[]);

#endif
