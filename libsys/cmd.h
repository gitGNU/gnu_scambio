/*
 * A mean to register keywords in order to have a runtime configuration.
 * These commands consists merely on a list of strings or numbers, the first
 * word being a keyword that allows a minimal syntax check.
 *
 * Commands are entered in a text file.
 *
 * May evolve into a lisp.
 */

#ifndef CMD_H_080616
#define CMD_H_080616
#include <queue.h>
#include "scambio.h"

union cmd_arg {
	char *string;
	long long integer;
};

typedef void cmd_callback(char const *keyword, unsigned nb_args, union cmd_arg *args);

#define CMD_MAX_ARGS 8

void cmd_begin(int textfd);
// keyword is not copied. Should be a static constant.
void cmd_register_keyword(char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, ...);
void cmd_unregister_keyword(char const *keyword);
void cmd_end(void);

void cmd_eval(int fd);

#endif
