/*
 * A mean to register keywords in order to have a runtime configuration.
 * These commands consists merely on a list of strings or numbers, the first
 * word being a keyword that allows a minimal syntax check.
 *
 * Commands are entered in a text file.
 */

#ifndef CMD_H_080616
#define CMD_H_080616
#include <queue.h>
#include "scambio.h"

union cmd_arg {
	char *string;
	long long integer;
};

typedef void cmd_callback(char const *token, unsigned nb_args, union cmd_arg *args);

#define CMD_MAX_ARGS 8

struct cmd_keyword {
	LIST_ENTRY(cmd_keyword) entry;
	char const *token;
	unsigned nb_arg_min, nb_arg_max;
	unsigned nb_types;	// after which STRING is assumed
	enum cmd_type { CMD_STRING, CMD_INTEGER, CMD_EOA } types[CMD_MAX_ARGS];
	cmd_callback *cb;
};

int cmd_begin(int textfd);
int cmd_register_keyword(char const *token, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, ...);
void cmd_unregister_keyword(char const *token);
void cmd_end(void);

#endif
