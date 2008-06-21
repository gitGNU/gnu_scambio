#ifndef CONF_H_080529
#define CONF_H_080529

#include <stdbool.h>
#include <sys/types.h>
#include <regex.h>

struct strib_test {
	struct strib_condition {
		enum strib_op { OP_ALWAYS, OP_SET, OP_UNSET, OP_GT, OP_GE, OP_LT, OP_LE, OP_EQ, OP_RE } op;
		char *field_name;
		unsigned field_key;
		enum value_type { TYPE_STRING, TYPE_NUMBER, TYPE_DEREF } value_type;
		union strib_value {
			long long number;
			char *string;
			struct strib_deref {
				char *name;
				unsigned key;
			} deref;
		} value;
		bool re_match_set;
		regex_t re_match;
	} condition;
	struct strib_action {
		enum action_type { ACTION_DISCARD, ACTION_COPY, ACTION_MOVE } type;
		enum dest_type { DEST_STRING, DEST_DEREF } dest_type;
		union strib_dest {
			char *string;
			struct strib_deref deref;
		} dest;
	} action;
};

#define NB_MAX_TESTS 1000	// FIXME: resizeable

struct stribution {
	unsigned nb_tests;
	struct strib_test tests[];	// Variable size
};

struct stribution *strib_new(char const *path);
void strib_del(struct stribution *);
void strib_dump(struct stribution *, void (*printer)(char const *fmt, ...));
// Returns the length of actions
struct header;
unsigned strib_eval(struct stribution *, struct header const *, struct strib_action const *actions[]);

#endif
