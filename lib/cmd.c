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
#include "scambio/cmd.h"
#include "varbuf.h"

/*
 * Data Definitions
 */

#define MAX_CMD_LINE (PATH_MAX + 256)

/*
 * Various inline functions
 */

extern inline void mdir_syntax_ctor(struct mdir_syntax *syntax, bool no_answer_if_no_seqnum);
extern inline void mdir_syntax_dtor(struct mdir_syntax *syntax);
extern inline void mdir_syntax_register(struct mdir_syntax *syntax, struct mdir_cmd_def *def);
extern inline void mdir_syntax_unregister(struct mdir_syntax *syntax, struct mdir_cmd_def *def);
extern inline void mdir_cmd_dtor(struct mdir_cmd *cmd);
extern inline char const *mdir_cmd_seq2str(char buf[SEQ_BUF_LEN], long long seq);

/*
 * Read a command from a file
 */

static bool same_keyword(char const *a, char const *b)
{
	return 0 == strcasecmp(a, b);
}

static void build_cmd(struct mdir_cmd *cmd, union mdir_cmd_arg *args)
{
	debug("new cmd for '%s', seqnum #%lld, %u args", cmd->def->keyword, cmd->seq, cmd->nb_args);
	if (cmd->nb_args < cmd->def->nb_arg_min || cmd->nb_args > cmd->def->nb_arg_max) with_error(EDOM, "Bad nb args (%u)", cmd->nb_args) return;
	for (unsigned a=0; a<cmd->nb_args; a++) {	// Copy and transcode args
		if (a < cmd->def->nb_types && cmd->def->types[a] == CMD_INTEGER) {	// transcode
			char *end;
			long long integer = strtoll(args[a].string, &end, 0);
			if (*end != '\0') with_error(EINVAL, "'%s' is not integer", args[a].string) return;
			cmd->args[a].integer = integer;
			debug("cmd arg %u -> %lld", a, cmd->args[a].integer);
		} else {	// duplicate the string
			cmd->args[a].string = strdup(args[a].string);
			if (! cmd->args[a].string) with_error(ENOMEM, "Cannot dup value %u", a) return;
			debug("cmd arg %u -> %s", a, cmd->args[a].string);
		}
	}
	return;
}

static bool is_delimiter(int c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0';
}

// vb stores a line terminated by \n\0 or just \0.
static int tokenize(struct varbuf *vb, union mdir_cmd_arg *tokens)
{
	int nb_tokens = 0;
	size_t c = 0;
	do {
		if (nb_tokens >= MAX_CMD_LINE) return -E2BIG;
		while (c < vb->used && is_delimiter(vb->buf[c])) c++;	// trim left
		if (c >= vb->used) break;
		tokens[nb_tokens].string = vb->buf+c;
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
	if (*end != '\0') with_error(EINVAL, "Cannot read token '%s'", str) return 0;
	debug("seq is %lld", *seq);
	return 0;
}

static bool is_seq(char const *str)
{
	debug("token : %s", str);
	return isdigit(str[0]) || (str[0] == '-' && isdigit(str[1]));
}

static void parse_line(struct mdir_syntax *syntax, struct mdir_cmd *cmd, struct varbuf *vb, int fd)
{
	varbuf_clean(vb);
	varbuf_read_line(vb, fd, MAX_CMD_LINE, NULL);
	on_error return;
	union mdir_cmd_arg tokens[1 + CMD_MAX_ARGS];	// will point into the varbuf
	int nb_tokens = tokenize(vb, tokens);
	on_error return;
	if (nb_tokens == 0) with_error(EINVAL, "No token found on line") return;
	bool with_seq = is_seq(tokens[0].string);
	if (with_seq && nb_tokens < 2) with_error(EINVAL, "Bad number of tokens (%d)", nb_tokens) return;
	char const *const keyword = tokens[with_seq ? 1:0].string;
	cmd->nb_args = nb_tokens - (with_seq ? 2:1);
	union mdir_cmd_arg *const args = tokens + (with_seq ? 2:1);
	cmd->seq = 0;
	if (with_seq) if_fail (read_seq(&cmd->seq, tokens[0].string)) return;
	LIST_FOREACH(cmd->def, &syntax->defs, entry) {
		if (cmd->seq < 0 && !cmd->def->negseq) continue;
		if (cmd->seq > 0 && cmd->def->negseq) continue;
		if (same_keyword(cmd->def->keyword, keyword)) {
			build_cmd(cmd, args);	// will check args types and number, and convert some args from string to integer
			if (cmd->seq < 0) cmd->seq = -cmd->seq;
			return;
		}
	}
	with_error(ENOENT, "No such keyword '%s'", keyword) return;
}

void mdir_cmd_read(struct mdir_syntax *syntax, int fd, void *user_data)
{
	debug("reading command on %d", fd);
	struct varbuf vb;
	varbuf_ctor(&vb, 1024, true);
	on_error return;
	struct mdir_cmd cmd;
	cmd.def = NULL;
	do {
		if_fail (parse_line(syntax, &cmd, &vb, fd)) break;
	} while (! cmd.def);
	varbuf_dtor(&vb);	// TODO: keep this vb until after the cb(), then we could ue the original strings in it instead of strduping
	unless_error {
		if (cmd.def->cb) {
			cmd.def->cb(&cmd, user_data);
		} else {
			debug("No handler for command '%s'", cmd.def->keyword);
		}
		mdir_cmd_dtor(&cmd);
	}
	return;
}

