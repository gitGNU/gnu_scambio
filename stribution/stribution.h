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
#ifndef CONF_H_080529
#define CONF_H_080529

#include <stdbool.h>
#include <sys/types.h>
#include <regex.h>

struct strib_test {
	struct strib_condition {
		enum strib_op { OP_ALWAYS, OP_SET, OP_UNSET, OP_GT, OP_GE, OP_LT, OP_LE, OP_EQ, OP_RE } op;
		char *field_name;
		enum value_type { TYPE_STRING, TYPE_NUMBER, TYPE_DEREF } value_type;
		union strib_value {
			long long number;
			char *string;
			struct strib_deref {
				char *name;
			} deref;
		} value;
		bool re_match_set;
		regex_t re_match;
	} condition;
	struct strib_action {
		enum action_type { ACTION_DELETE, ACTION_COPY, ACTION_MOVE } type;
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

struct header;
struct stribution *strib_new(struct header const *);
void strib_del(struct stribution *);
void strib_dump(struct stribution const *, void (*printer)(char const *fmt, ...));
void strib_eval(struct stribution *, struct header const *, void (*action)(struct header const *, struct strib_action const *, void *data), void *data);

#endif
