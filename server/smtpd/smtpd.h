#ifndef SMTPD_H_080623
#define SMTPD_H_080623

#include <pth.h>
#include "queue.h"

struct header;

struct cnx_env {
	int fd;
	struct header *h;
	char start_head[200];
};

int exec_begin(void);
void exec_end(void);
int exec_ehlo(struct cnx_env *, char const *client_id);
int exec_helo(struct cnx_env *, char const *client_id);
int exec_mail(struct cnx_env *, char const *from);
int exec_rcpt(struct cnx_env *, char const *to);
int exec_data(struct cnx_env *);
int exec_rset(struct cnx_env *);
int exec_vrfy(struct cnx_env *, char const *user);
int exec_expn(struct cnx_env *, char const *list);
int exec_help(struct cnx_env *, char const *command);
int exec_noop(struct cnx_env *);
int exec_quit(struct cnx_env *);

#endif
