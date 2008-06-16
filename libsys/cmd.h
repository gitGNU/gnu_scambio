/*
 * A mean to register keywords in order to have a runtime configuration.
 * These commands consists merely on a list of strings or numbers, the first
 * word being a keyword that allows a minimal syntax check.
 *
 * Commands are entered in an arch independant text stream.
 * A text-stream is created and its callbacks are used to read lines, which are
 * then parsed. The text-stream is given as a parameter to the begin function.
 */

#ifndef CMD_H_080616
#define CMD_H_080616
#include <queue.h>
#include "scambio.h"
#include "textstream.h"

union cmd_arg {
	char *string;
	long long integer;
};

typedef void cmd_callback(char const *token, unsigned nb_args, union cmd_arg *args);

#define CMX_MAX_ARGS 8

struct cmd_keyword {
	LIST_ENTRY(cmd_keyword) entry;
	char const *token;
	unsigned nb_arg_min, nb_arg_max;
	unsigned nb_types;	// after which STRING is assumed
	enum cmd_type { CMD_STRING, CMD_INTEGER, CMD_EOA } types[CMD_MAX_ARGS];
	cmd_callback *cb;
};

int cmd_begin(struct textstream *textstream);
int cmd_register_keyword(char const *token, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, ...);
void cmd_unregister_keyword(char const *token);
void cmd_end(void);

#endif
