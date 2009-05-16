%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <log.h>
#include "scambio.h"
#include "stribution.h"
 
extern int lineno;
void yyerror(char const *);
extern int yylex();
extern FILE *yyin;

static struct stribution *stribution;
#define TEST (stribution->tests[stribution->nb_tests])

#define YYSTYPE char *

extern YYSTYPE yytext;

static enum strib_op str2binaryop(char const *str)
{
	static struct {
		char const *str;
		size_t len;
		enum strib_op op;
	} const binaryops[] = {	// Put longest string first !
		{ ">=", 2, OP_GE }, { "<=", 2, OP_LE }, { "=~", 2, OP_RE },
		{ "=", 1, OP_EQ }, { ">", 1, OP_GT }, { "<", 1, OP_LT },
	};
	for (unsigned i=0; i<sizeof_array(binaryops); i++) {
		if (0 == strncmp(binaryops[i].str, str, binaryops[i].len)) return binaryops[i].op;
	}
	yyerror("Unknown binary operator");
	return -1;
}

static enum strib_op str2unaryop(char const *str)
{
	static struct {
		char const *str;
		size_t len;
		enum strib_op op;
	} const unaryops[] = {
		{ "unset", 5, OP_UNSET }, { "set", 3, OP_SET },
	};
	for (unsigned i=0; i<sizeof_array(unaryops); i++) {
		if (0 == strncmp(unaryops[i].str, str, unaryops[i].len)) return unaryops[i].op;
	}
	yyerror("Unknown unary operator");
	return -1;
}

static enum action_type str2destaction(char const *str)
{
	static struct {
		char const *str;
		size_t len;
		enum action_type type;
	} const destactions[] = {
		{ "copyto", 5, ACTION_COPY }, { "moveto", 5, ACTION_MOVE },
	};
	for (unsigned i=0; i<sizeof_array(destactions); i++) {
		if (0 == strncmp(destactions[i].str, str, destactions[i].len)) return destactions[i].type;
	}
	yyerror("Unknown destination-action");
	return -1;
}

// Return a copy of str without the enclosing quotes
static char *copystring(char const *str)
{
	size_t len = strlen(str);
	assert(len >= 2);
	char *new = malloc(len-1);
	memcpy(new, str+1, len-2);
	new[len-2] = '\0';
	return new;
}

%}

%token NUMBER SEPARATOR STRING FIELDNAME EOL ALWAYS UNARYOP BINARYOP DISCARD DESTACTION DEREF
%debug
%%

tests:
	tests test {
		if (++ stribution->nb_tests >= NB_MAX_TESTS) {
			yyerror("Too many tests (max is "TOSTR(NB_MAX_TESTS)")");
			return -1;
		}
	} |
	;

test:
	condition SEPARATOR action EOL
	;

condition:
	ALWAYS {
		TEST.condition.op = OP_ALWAYS;
	}
	| field BINARYOP value {
		TEST.condition.op = str2binaryop($2);
		if (TEST.condition.op == OP_RE) {	// Only with pure string values
			if (TEST.condition.value_type == TYPE_DEREF) {
				yyerror("Cannot use regex with varying field values");
				return -1;
			}
			if (0 != regcomp(&TEST.condition.re_match, TEST.condition.value.string, REG_EXTENDED|REG_ICASE|REG_NOSUB)) {
				regfree(&TEST.condition.re_match);
				yyerror("Cannot compile regular expression");
				return -1;
			}
			TEST.condition.re_match_set = true;
		}
	}
	| field UNARYOP {
		TEST.condition.op = str2unaryop($2);
	}
	;

field:
	FIELDNAME {
		TEST.condition.field_name = strdup($1);
	}
	;

value:
	STRING {
		TEST.condition.value_type   = TYPE_STRING;
		TEST.condition.value.string = copystring($1);
	}
	| NUMBER {
		TEST.condition.value_type   = TYPE_NUMBER;
		TEST.condition.value.number = strtoll($1, NULL, 0);
	}
	| deref {
		TEST.condition.value_type       = TYPE_DEREF;
		TEST.condition.value.deref.name = strdup($1);
	}
	;

action:
	DISCARD {
		TEST.action.type = ACTION_DISCARD;
	}
	| DESTACTION dest {
		TEST.action.type = str2destaction($1);
	}
	;

dest:
	STRING {
		TEST.action.dest_type   = DEST_STRING;
		TEST.action.dest.string = copystring($1);
	}
	| deref {
		TEST.action.dest_type       = DEST_DEREF;
		TEST.action.dest.deref.name = strdup($1);
	}
	;

deref:
	DEREF FIELDNAME { $$ = $2; }
	;

%%

void yyerror(const char *str)
{
	error("Error line %d @ '%s': %s\n", lineno, yytext, str);
}

int yywrap()
{
	return 1;
} 

int strib_parse(char const *filename, struct stribution *c)
{
	stribution = c;
	FILE *file = fopen(filename, "r");
	if (! file) {
		error("Cannot open configuration file '%s' : %s", filename, strerror(errno));
		return -1;
	}
	yyin = file;
	yydebug = 1;
	lineno = 1;
	int ret = 0;
	do {
		if (0 != yyparse()) {
			ret = -1;
			break;
		}
	} while (!feof(yyin));
	fclose(file);
	yyin = NULL;
	return ret;
}

