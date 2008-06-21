#include <stdarg.h>
#include <string.h>
#include <miscmac.h>
#include <exception.h>
#include "cmd.h"

/*
 * Data Definitions
 */

struct cmd {
	LIST_ENTRY(cmd) entry;
	char const *keyword;
	unsigned nb_arg_min, nb_arg_max;
	cmd_callback *cb;
	unsigned nb_types;	// after which STRING is assumed
	enum cmd_type { CMD_STRING, CMD_INTEGER, CMD_EOA } types[CMD_MAX_ARGS];
};

static LIST_HEAD(cmds, cmd) cmds;
static int textfd;

/*
 * Command (un)registration
 */

static void cmd_ctor(struct cmd *cmd, char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, va_list ap)
{
	cmd->keyword = keyword;
	cmd->nb_arg_min = nb_arg_min;
	cmd->nb_arg_max = nb_arg_max;
	cmd->cb = cb;
	cmd->nb_types = 0;
	enum cmd_type type;
	while (CMD_EOA != (type = va_arg(ap, enum cmd_type))) {
		if (cmd->nb_types >= sizeof_array(cmd->types)) THROW(exception_bad_param("too many types"));
		cmd->types[ cmd->nb_types++ ] = type;
	}
	LIST_INSERT_HEAD(&cmds, cmd, entry);
}

static void cmd_dtor(struct cmd *cmd)
{
	LIST_REMOVE(cmd, entry);
}

static struct cmd *cmd_new(char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, va_list ap)
{
	struct cmd *cmd = malloc_or_throw_uwprotect(sizeof(*cmd));
	cmd_ctor(cmd, keyword, nb_arg_min, nb_arg_max, cb, ap);
	// Should we keep initializing stuff with functions that could trigger an exception, we would add :
	// atunwind(cmd_dtor, cmd)
	// so that the exception would both destruct and free the cmd.
	dewind;
	return cmd;
}

static void cmd_del(struct cmd *cmd)
{
	cmd_dtor(cmd);
	free(cmd);
}

void cmd_register_keyword(char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, ...)
{
	va_list ap;
	va_start(ap, cb);
	// FIXME: check that keyword is not already registered ?
	(void)cmd_new(keyword, nb_arg_min, nb_arg_max, cb, ap);
	va_end(ap);
}

void cmd_unregister_keyword(char const *keyword)
{
	struct cmd *cmd, *tmp;
	LIST_FOREACH_SAFE(cmd, &cmds, entry, tmp) {
		if (0 == strcmp(cmd->keyword, keyword)) {
			cmd_del(cmd);
		}
	}
}

/*
 * (De)Initialization
 */

void cmd_begin(int textfd_)
{
	textfd = textfd_;
	LIST_INIT(&cmds);
}

void cmd_end(void)
{
	struct cmd *cmd;
	while (NULL != (cmd = LIST_FIRST(&cmds))) {
		cmd_del(cmd);
	}
}

/*
 * Eval a line
 */

void cmd_eval(int fd)
{
	struct varbuf varbuf;
	union cmd_arg *tokens[1 + CMD_MAX_ARGS];	// will point into the varbuf
	unsigned nb_tokens;
	varbuf_ctor(&varbuf, 1024, true);
	atunwind(varbuf_dtor, &varbuf);
	varbuf_gets(&varbuf, fd);
	unsigned nb_tokens = tokenize(&varbuf, tokens);
	if (nb_tokens) {
		char const *const keyword = tokens[0]->string;
		unsigned nb_args = nb_tokens - 1;
		union cmd_arg *const args = tokens+1;
		struct cmd *cmd;
		LIST_FOREACH(cmd, &cmds, entry) {
			if (same_keyword(cmd->keyword, keyword)) {
				check_args(cmd, nb_args, args);	// will also convert some args from string to integer
				cmd->cb(keyword, nb_args, args);
				break;
			}
		}
	}
	unwind;
}

