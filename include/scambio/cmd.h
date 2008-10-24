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
#include <scambio/queue.h>

#define CMD_MAX_ARGS 8

struct mdir_cmd;
typedef void mdir_cmd_cb(struct mdir_cmd *cmd, void *user_data);

/* This defines a valid command, and keeps a pointer to the proper callback.
 * It can be inherited.
 */
struct mdir_cmd_def {
	LIST_ENTRY(mdir_cmd_def) entry;
	char const *keyword;
	mdir_cmd_cb *cb;
	unsigned nb_arg_min, nb_arg_max;
	unsigned nb_types;	// after which STRING is assumed
	enum mdir_cmd_arg_type { CMD_STRING, CMD_INTEGER } types[CMD_MAX_ARGS];
};

/* This structure stores a command, once parsed and once the syntax is checked against its definition.
 */
struct mdir_cmd {
	struct mdir_cmd_def *def;
	long long seq;	// -1 if no seqnum was read
	unsigned nb_args;
	union mdir_cmd_arg {	// actual type is taken from the definition. Past def->nb_arg_max it's STRING.
		char *string;
		long long integer;
	} args[CMD_MAX_ARGS];
};

/* A syntax is then merely a set of definitions.
 * Can also be inherited.
 */
struct mdir_syntax {
	LIST_HEAD(cmd_defs, mdir_cmd_def) defs;
};

/* Construct and destruct a syntax.
 */
static inline void mdir_syntax_ctor(struct mdir_syntax *syntax)
{
	LIST_INIT(&syntax->defs);
}
static inline void mdir_syntax_dtor(struct mdir_syntax *syntax)
{
	struct mdir_cmd_def *def;
	while (NULL != (def = LIST_FIRST(&syntax->defs))) {
		LIST_REMOVE(def, entry);
	}
}

/* Add the given def to the syntax (merely link to the defs).
 */
static inline void mdir_syntax_register(struct mdir_syntax *syntax, struct mdir_cmd_def *def)
{
	LIST_INSERT_HEAD(&syntax->defs, def, entry);
}

static inline void mdir_syntax_unregister(struct mdir_syntax *syntax, struct mdir_cmd_def *def)
{
	(void)syntax;
	LIST_REMOVE(def, entry);
}

/* Read a line from fd, and parse it according to syntax.
 * The registered callback will be called.
 */
void mdir_cmd_read(struct mdir_syntax *syntax, int fd, void *user_data);

#include <stdlib.h>
static inline void mdir_cmd_dtor(struct mdir_cmd *cmd)
{
	for (unsigned a=0; a<cmd->nb_args; a++) {	// Copy and transcode args
		if (a >= cmd->def->nb_arg_max || cmd->def->types[a] == CMD_STRING) free(cmd->args[a].string);
	}
}

/* Utility function to convert a seqnum to a string
 */
#define SEQ_BUF_LEN 21
#include <stdio.h>
static inline char const *mdir_cmd_seq2str(char buf[SEQ_BUF_LEN], long long seq)
{
	snprintf(buf, sizeof(buf), "%lld", seq);
	return buf;
}

#endif
