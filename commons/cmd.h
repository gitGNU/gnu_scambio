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
	long long seq;	// -1 if no seqnum was read
	unsigned nb_args;
	struct cmd_arg args[CMD_MAX_ARGS];
};

void cmd_begin(void);
// keyword is not copied. Should be a static constant.
void cmd_register_keyword(char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, ...);
void cmd_unregister_keyword(char const *keyword);
void cmd_end(void);
// Returns -1 on EOF.
// construct a struct cmd that must be destroyed with cmd_dtor
int cmd_read(struct cmd *cmd, int fd);
void cmd_dtor(struct cmd *cmd);
#define SEQ_BUF_LEN 21
char const *cmd_seq2str(char buf[SEQ_BUF_LEN], long long seq);

#endif
