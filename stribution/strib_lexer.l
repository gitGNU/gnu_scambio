%{
#include <stdio.h>
#include "config.h"
#include "strib_parser.h"

static void __attribute__((unused)) yyunput (int,char *);	// Avoids a warning
int lineno;
%}

%%

[-+]?[0-9]+	        yylval = yytext; return NUMBER;
:                   return SEPARATOR;
!                   return DEREF;
always              return ALWAYS;
unset|set           yylval = yytext; return UNARYOP;
\<|\<=|\>|\>=|=~|=  yylval = yytext; return BINARYOP;
dielete             return DELETE;
copyto|moveto       yylval = yytext; return DESTACTION;
\"[^"]*\"           yylval = yytext; return STRING;
[a-zA-Z][-a-zA-Z0-9:_/.@]* yylval = yytext; return FIELDNAME;
[ \t]               /* ignore */
\n                  lineno++; return EOL;
