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
 * Also used as a base for the network protocols (mdirs/channels)
 */

#ifndef CMD_H_080616
#define CMD_H_080616
#include <stdbool.h>

struct mdir_cmd_arg {
	enum mdir_cmd_arg_type { CMD_STRING, CMD_INTEGER, CMD_EOA } type;
	union {
		char *string;
		long long integer;
	} val;
};

#define CMD_MAX_ARGS 8

struct mdir_cmd {
	char const *keyword;	// keywords are compared eq by address
	long long seq;	// 0 if no seqnum was read (0 is then an invalid seqnum)
	unsigned nb_args;
	struct mdir_cmd_arg args[CMD_MAX_ARGS];
};

struct mdir_registered_cmd;
struct mdir_parser {
	LIST_HEAD(rcmds, mdir_registered_cmd) rcmds;
};

void mdir_parser_ctor(struct mdir_parser *);
void mdir_parser_dtor(struct mdir_parser *);
// keyword is not copied. Should be a static constant.
void mdir_parser_register_keyword(struct mdir_parser *, char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, ...);
void mdir_parser_unregister_keyword(struct mdir_parser *, char const *keyword);
// construct a struct cmd that must be destroyed with mdir_cmd_dtor
void mdir_cmd_read(struct mdir_parser *, struct mdir_cmd *, int fd);
void mdir_cmd_dtor(struct mdir_cmd *);
#define SEQ_BUF_LEN 21
char const *mdir_cmd_seq2str(char buf[SEQ_BUF_LEN], long long seq);

#endif
