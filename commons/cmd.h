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
#include <stdbool.h>
#include <queue.h>
#include "scambio.h"

struct cmd_arg {
	enum cmd_arg_type { CMD_STRING, CMD_INTEGER, CMD_EOA } type;
	union {
		char *string;
		long long integer;
	} val;
};

#define CMD_MAX_ARGS 8

struct cmd {
	char const *keyword;	// the same pointer than the one registered
	long long seq;
	unsigned nb_args;
	struct cmd_arg args[CMD_MAX_ARGS];
};

void cmd_begin(void);
// keyword is not copied. Should be a static constant.
void cmd_register_keyword(char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, ...);
void cmd_unregister_keyword(char const *keyword);
void cmd_end(void);
// Returns cmd->keyword = NULL on EOF.
// construct a struct cmd that must be destroyed with cmd_dtor
int cmd_read(struct cmd *cmd, bool with_seq, int fd);
void cmd_dtor(struct cmd *cmd);

#endif
