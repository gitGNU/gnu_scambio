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
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <strings.h>
#include <ctype.h>
#include "scambio.h"
#include "cmd.h"
#include "varbuf.h"

/*
 * Data Definitions
 */

#define MAX_CMD_LINE (PATH_MAX + 256)

struct registered_cmd {
	LIST_ENTRY(registered_cmd) entry;
	char const *keyword;
	unsigned nb_arg_min, nb_arg_max;
	unsigned nb_types;	// after which STRING is assumed
	enum cmd_arg_type types[CMD_MAX_ARGS];
};

/*
 * Command (un)registration
 */

static void rcmd_ctor(struct cmd_parser *cmdp, struct registered_cmd *reg, char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, va_list ap)
{
	reg->keyword = keyword;
	reg->nb_arg_min = nb_arg_min;
	reg->nb_arg_max = nb_arg_max;
	reg->nb_types = 0;
	enum cmd_arg_type type;
	while (CMD_EOA != (type = va_arg(ap, enum cmd_arg_type))) {
		if (reg->nb_types >= sizeof_array(reg->types)) with_error(E2BIG, "Too many commands") return;
		reg->types[ reg->nb_types++ ] = type;
	}
	LIST_INSERT_HEAD(&cmdp->rcmds, reg, entry);	// will shadow previously registered keywords
}

static void rcmd_dtor(struct registered_cmd *reg)
{
	LIST_REMOVE(reg, entry);
}

static struct registered_cmd *rcmd_new(struct cmd_parser *cmdp, char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, va_list ap)
{
	struct registered_cmd *reg = malloc(sizeof(*reg));
	if (! reg) with_error(ENOMEM, "Cannot alloc cmd") return NULL;
	rcmd_ctor(cmdp, reg, keyword, nb_arg_min, nb_arg_max, ap);
	on_error {
		free(reg);
		return NULL;
	}
	return reg;
}

static void rcmd_del(struct registered_cmd *reg)
{
	rcmd_dtor(reg);
	free(reg);
}

void cmd_register_keyword(struct cmd_parser *cmdp, char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, ...)
{
	va_list ap;
	va_start(ap, nb_arg_max);
	(void)rcmd_new(cmdp, keyword, nb_arg_min, nb_arg_max, ap);
	va_end(ap);
}

void cmd_unregister_keyword(struct cmd_parser *cmdp, char const *keyword)
{
	struct registered_cmd *reg, *tmp;
	LIST_FOREACH_SAFE(reg, &cmdp->rcmds, entry, tmp) {
		if (reg->keyword == keyword) {
			rcmd_del(reg);
			break;	// removes only the topmost matching keyword, so that shadowed keywords are still there
		}
	}
}

/*
 * (De)Initialization
 */

void cmd_parser_ctor(struct cmd_parser *cmdp)
{
	LIST_INIT(&cmdp->rcmds);
}

void cmd_parser_dtor(struct cmd_parser *cmdp)
{
	struct registered_cmd *reg;
	while (NULL != (reg = LIST_FIRST(&cmdp->rcmds))) {
		rcmd_del(reg);
	}
}

/*
 * Read a commend from a file
 */

static bool same_keyword(char const *a, char const *b)
{
	return 0 == strcasecmp(a, b);
}

static void build_cmd(struct cmd *cmd, struct registered_cmd *reg, unsigned nb_args, struct cmd_arg *args)
{
	cmd->keyword = reg->keyword;	// set original pointer
	cmd->nb_args = nb_args;
	if (nb_args < reg->nb_arg_min || nb_args > reg->nb_arg_max) with_error(EDOM, "Bad nb args") return;
	for (unsigned a=0; a<nb_args; a++) {	// Copy and transcode args
		if (a < reg->nb_types && reg->types[a] == CMD_INTEGER) {	// transcode
			char *end;
			long long integer = strtoll(args[a].val.string, &end, 0);
			if (*end != '\0') with_error(EINVAL, "'%s' is not integer", args[a].val.string) return;
			cmd->args[a].type = CMD_INTEGER;
			cmd->args[a].val.integer = integer;
		} else {
			cmd->args[a].type = CMD_STRING;
			cmd->args[a].val.string = strdup(args[a].val.string);
		}
	}
	return;
}

void cmd_dtor(struct cmd *cmd)
{
	for (unsigned a=0; a<cmd->nb_args; a++) {	// Copy and transcode args
		if (cmd->args[a].type == CMD_STRING) free(cmd->args[a].val.string);
	}
}

static bool is_delimiter(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
}

// vb stores a line terminated by \n\0 or just \0.
static int tokenize(struct varbuf *vb, struct cmd_arg *tokens)
{
	int nb_tokens = 0;
	size_t c = 0;
	do {
		if (nb_tokens >= MAX_CMD_LINE) return -E2BIG;
		while (c < vb->used && is_delimiter(vb->buf[c])) c++;	// trim left
		if (c >= vb->used) break;
		tokens[nb_tokens].type = CMD_STRING;
		tokens[nb_tokens].val.string = vb->buf+c;
		while (c < vb->used && !is_delimiter(vb->buf[c])) c++;	// reach end of token
		if (c >= vb->used) break;
		vb->buf[c] = '\0';	// replace first delimiter with '\0'
		nb_tokens ++;
	} while (1);
	return nb_tokens;
}

static int read_seq(long long *seq, char const *str)
{
	char *end;
	*seq = strtoll(str, &end, 0);
	if (*end != '\0') return -EINVAL;
	return 0;
}

static void parse_line(struct cmd_parser *cmdp, struct cmd *cmd, struct varbuf *vb, int fd)
{
	varbuf_clean(vb);
	varbuf_read_line(vb, fd, MAX_CMD_LINE, NULL);
	on_error return;
	struct cmd_arg tokens[1 + CMD_MAX_ARGS];	// will point into the varbuf
	int nb_tokens = tokenize(vb, tokens);
	on_error return;
	if (nb_tokens == 0) with_error(EINVAL, "No token found on line") return;
	bool with_seq = isdigit(tokens[0].val.string[0]);
	if (with_seq && nb_tokens < 2) with_error(EINVAL, "Bad number of tokens (%d)", nb_tokens) return;
	char const *const keyword = tokens[with_seq ? 1:0].val.string;
	unsigned nb_args = nb_tokens - (with_seq ? 2:1);
	struct cmd_arg *const args = tokens + (with_seq ? 2:1);
	cmd->seq = -1;
	if (with_seq && read_seq(&cmd->seq, tokens[0].val.string) && is_error()) return;
	struct registered_cmd *reg;
	LIST_FOREACH(reg, &cmdp->rcmds, entry) {
		if (same_keyword(reg->keyword, keyword)) {
			build_cmd(cmd, reg, nb_args, args);	// will check args types and number, and convert some args from string to integer, and copy nb_args and keyword
			return;
		}
	}
	with_error(ENOENT, "No such keyword '%s'", keyword) return;
}

void cmd_read(struct cmd_parser *cmdp, struct cmd *cmd, int fd)
{
	struct varbuf vb;
	varbuf_ctor(&vb, 1024, true);
	on_error return;
	cmd->keyword = NULL;
	while (! cmd->keyword) {
		parse_line(cmdp, cmd, &vb, fd);
		on_error break;
	}
	varbuf_dtor(&vb);
	return;
}

char const *cmd_seq2str(char buf[SEQ_BUF_LEN], long long seq)
{
	snprintf(buf, sizeof(buf), "%lld", seq);
	return buf;
}

