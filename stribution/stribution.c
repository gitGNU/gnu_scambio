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
#include <stdbool.h>
#include <string.h>
#include <log.h>
#include <assert.h>
#include "scambio/header.h"
#include "stribution.h"

/*
 * Data Definitions
 */

int strib_parse(char const *filename, struct stribution *c);

/*
 * Private Functions
 */

static char const *op2str(enum strib_op op)
{
	switch (op) {
		case OP_ALWAYS: return "always";
		case OP_SET:    return "set";
		case OP_UNSET:  return "unset";
		case OP_GT:     return ">";
		case OP_GE:     return ">=";
		case OP_LT:     return "<";
		case OP_LE:     return "<=";
		case OP_EQ:     return "=";
		case OP_RE:     return "=~";
	}
	return "INVALID";
}

static char const *action2str(enum action_type type)
{
	switch (type) {
		case ACTION_DELETE: return "delete";
		case ACTION_COPY:   return "copyto";
		case ACTION_MOVE:   return "moveto";
	}
	return "INVALID";
}

static bool is_constant(enum strib_op op)
{
	return op == OP_ALWAYS;
}

static bool is_unary(enum strib_op op)
{
	return op == OP_SET || op == OP_UNSET;
}

static bool is_binary(enum strib_op op)
{
	return op == OP_GT || op == OP_GE || op == OP_LT || op == OP_LE || op == OP_EQ || op == OP_RE;
}

static bool has_dest(enum action_type type)
{
	return type != ACTION_DELETE;
}

static void test_dump(struct strib_test *test, void (*printer)(char const *fmt, ...))
{
	if (is_constant(test->condition.op)) {
		printer("%s", op2str(test->condition.op));
	} else {
		printer("%s %s", test->condition.field_name, op2str(test->condition.op));
		if (! is_unary(test->condition.op)) {
			switch (test->condition.value_type) {
				case TYPE_STRING: printer(" \"%s\"", test->condition.value.string);       break;
				case TYPE_NUMBER: printer(" %lld", test->condition.value.number);     break;
				case TYPE_DEREF:  printer(" ! %s", test->condition.value.deref.name); break;
			}
		}
	}
	printer(" : %s", action2str(test->action.type));
	if (has_dest(test->action.type)) {
		switch (test->action.dest_type) {
			case DEST_STRING: printer(" \"%s\"", test->action.dest.string);       break;
			case DEST_DEREF:  printer(" ! %s", test->action.dest.deref.name); break;
		}
	}
	printer("\n");
}

static void strib_condition_dtor(struct strib_condition *cond)
{
	// Destruct condition
	switch (cond->op) {
		case OP_ALWAYS:
			break;
		case OP_SET:
		case OP_UNSET:
			free(cond->field_name);
			break;
		case OP_GT:
		case OP_GE:
		case OP_LT:
		case OP_LE:
		case OP_EQ:
		case OP_RE:
			free(cond->field_name);
			switch (cond->value_type) {
				case TYPE_STRING:
					free(cond->value.string);
					break;
				case TYPE_NUMBER:
					break;
				case TYPE_DEREF:
					free(cond->value.deref.name);
					break;
			}
			if (cond->re_match_set) regfree(&cond->re_match);
			break;
	}
}

static void strib_action_dtor(struct strib_action *a)
{
	switch (a->type) {
		case ACTION_DELETE:
			break;
		case ACTION_COPY:
		case ACTION_MOVE:
			switch (a->dest_type) {
				case DEST_STRING:
					free(a->dest.string);
					break;
				case DEST_DEREF:
					free(a->dest.deref.name);
					break;
			}
			break;
	}
}

static void test_dtor(struct strib_test *t)
{
	strib_condition_dtor(&t->condition);
	strib_action_dtor(&t->action);
}

static void strib_dtor(struct stribution *c)
{
	for (unsigned t=0; t<c->nb_tests; t++) {
		test_dtor(c->tests+t);
	}
}

static bool binary_op_num(enum strib_op op, long long a, long long b)
{
	switch (op) {
		case OP_GT: return a > b;
		case OP_GE: return a >= b;
		case OP_LT: return a < b;
		case OP_LE: return a <= b;
		case OP_EQ: return a == b;
		case OP_RE: warning("regular expression used on integers"); return false;
		default:    assert(0);
	}
	return false;
}

static bool binary_op_str(enum strib_op op, char const *a, char const *b)
{
	int cmp = strcmp(a, b);
	switch (op) {
		case OP_GT: return cmp > 0;
		case OP_GE: return cmp >= 0;
		case OP_LT: return cmp < 0;
		case OP_LE: return cmp <= 0;
		case OP_EQ: return cmp == 0;
		case OP_RE: assert(0);
		default:    assert(0);
	}
	return false;
}

static bool re_match_str(char const *a, regex_t *re)
{
	return 0 == regexec(re, a, 0, NULL, 0);
}

static int str2num(long long *num, char const *str)
{
	char *end;
	*num = strtoll(str, &end, 0);
	return *end == '\0' ? 0:-1;
}

static bool field_is_set(char const *field_name, struct header const *head)
{
	return NULL != header_search(head, field_name);
}

static bool condition_eval(struct strib_condition *cond, struct header const *head)
{
	switch (cond->op) {
		case OP_ALWAYS: return true;
		// Unary opes
		case OP_SET:    return field_is_set(cond->field_name, head);
		case OP_UNSET:  return !field_is_set(cond->field_name, head);
		// Binary ops
		default:        break;
	}
	assert(is_binary(cond->op));
	// We need the field value
	char const *field_value = header_search(head, cond->field_name);
	long long field_num;
	if (! field_value) return false;
	// We need value for binary ops
	bool has_num = false;
	char const *str_value = NULL;
	long long num_value;
	switch (cond->value_type) {
		case TYPE_STRING:
			str_value = cond->value.string;
			break;
		case TYPE_NUMBER:
			num_value = cond->value.number;
			has_num = true;
			break;
		case TYPE_DEREF:
			str_value = header_search(head, cond->value.deref.name);
			break;
	}
	// Now cast field value to what we need
	if (has_num) {
		if (-1 == str2num(&field_num, field_value)) {
			warning("Cannot compare integer %lld with field %s value '%s'", num_value, cond->field_name, field_value);
			return false;
		}
		return binary_op_num(cond->op, field_num, num_value);
	}
	if (cond->op == OP_RE) return re_match_str(field_value, &cond->re_match);
	return binary_op_str(cond->op, field_value, str_value);
}

/*
 * Public Functions
 */

struct stribution *strib_new(char const *filename)
{
	struct stribution *c = calloc(1, sizeof(*c) + NB_MAX_TESTS*sizeof(c->tests[0]));
	if (! c) {
		error("Cannot alloc a stribution for '%s'", filename);
		return NULL;
	}
	if (0 != strib_parse(filename, c)) {
		free(c);
		return NULL;
	}
	struct stribution *smaller_c = realloc(c, sizeof(*c) + c->nb_tests*sizeof(c->tests[0]));
	if (! smaller_c) {
		warning("Cannot resize stribution for '%s'", filename);
		return c;
	}
	return smaller_c;
}

void strib_del(struct stribution *stribution)
{
	strib_dtor(stribution);
	free(stribution);
}

void strib_dump(struct stribution *stribution, void (*printer)(char const *fmt, ...))
{
	printer("Configuration :\n");
	for (unsigned t=0; t<stribution->nb_tests; t++) {
		test_dump(stribution->tests+t, printer);
	}
}

unsigned strib_eval(struct stribution *stribution, struct header const *head, struct strib_action const *actions[])
{
	unsigned nb_actions = 0;
	for (unsigned t=0; t<stribution->nb_tests; t++) {
		if (condition_eval(&stribution->tests[t].condition, head)) {
			actions[nb_actions++] = &stribution->tests[t].action;
			// TODO: break once a final state is reached (delete or move)
		}
	}
	return nb_actions;
}

