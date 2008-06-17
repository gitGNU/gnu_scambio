#include "cmd.h"

/*
 * Data Definitions
 */

struct cmd {
	LIST_ENTRY(cmd_keyword) entry;
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
		if (cmd->nb_types >= sizeof_array(cmd->types)) THROW(BAD_PARAMETER);
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
	struct cmd *cmd = malloc(sizeof(*cmd));
	if (! cmd) THROW(OOM);
	unwindb();
	atunwind(free, cmd);
	cmd_ctor(cmd, keyword, nb_arg_min, nb_arg_max, cb, ap);

}

int cmd_register_keyword(char const *keyword, unsigned nb_arg_min, unsigned nb_arg_max, cmd_callback *cb, ...);
void cmd_unregister_keyword(char const *keyword);


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


