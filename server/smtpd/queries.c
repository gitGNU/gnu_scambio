#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <pth.h>
#include "scambio.h"
#include "smtpd.h"
#include "misc.h"
#include "varbuf.h"
#include "header.h"
#include "persist.h"

/*
 * (De)Init
 */

int exec_begin(void)
{
	return 0;
}

void exec_end(void) {
}

/*
 * Answers
 */

#define CRLF "\r\n"
static int answer_gen(struct cnx_env *env, int status, char const *cmpl, char sep)
{
	char line[100];
	size_t len = snprintf(line, sizeof(line), "%03d%c%s"CRLF, status, sep, cmpl);
	assert(len < sizeof(line));
	return Write(env->fd, line, len);
}
static int answer_cont(struct cnx_env *env, int status, char *cmpl)
{
	return answer_gen(env, status, cmpl, '-');
}
static int answer(struct cnx_env *env, int status, char *cmpl)
{
	return answer_gen(env, status, cmpl, ' ');
}

/*
 * HELO
 */

int exec_ehlo(struct cnx_env *env, char const *client_id)
{
	return -1;
}
int exec_helo(struct cnx_env *env, char const *client_id)
{
	return -1;
}

/*
 * MAIL/RCPT
 */

int exec_mail(struct cnx_env *env, char const *from)
{
	return -1;
}
int exec_rcpt(struct cnx_env *env, char const *to)
{
	return -1;
}

/*
 * DATA
 */

int exec_data(struct cnx_env *env)
{
	return -1;
}

/*
 * Other commands
 */

int exec_rset(struct cnx_env *env)
{
	return -1;
}
int exec_vrfy(struct cnx_env *env, char const *user)
{
	return -1;
}
int exec_expn(struct cnx_env *env, char const *list)
{
	return -1;
}
int exec_help(struct cnx_env *env, char const *command)
{
	return -1;
}
int exec_noop(struct cnx_env *env)
{
	return -1;
}
int exec_quit(struct cnx_env *env)
{
	return -1;
}

