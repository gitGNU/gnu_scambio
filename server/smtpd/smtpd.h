#ifndef SMTPD_H_080623
#define SMTPD_H_080623

#include <pth.h>
#include "queue.h"
#include "varbuf.h"

#define CRLF "\r\n"

extern char my_hostname[256];

struct cnx_env {
	int fd;
	char *domain;	// stores the last received helo information
	char *reverse_path;
	char *forward_path;
	char client_address[100];
	char reception_date[10+1];
	char reception_time[8+1];
	// From the top-level header (points toward a varbuf that will be deleted by the time this env is deleted)
	char const *subject, *message_id;
};

// From queries.c

int exec_begin(void);
void exec_end(void);
int answer(struct cnx_env *env, int status, char *cmpl);
int exec_ehlo(struct cnx_env *, char const *domain);
int exec_helo(struct cnx_env *, char const *domain);
int exec_mail(struct cnx_env *, char const *from);
int exec_rcpt(struct cnx_env *, char const *to);
int exec_data(struct cnx_env *);
int exec_rset(struct cnx_env *);
int exec_vrfy(struct cnx_env *, char const *user);
int exec_expn(struct cnx_env *, char const *list);
int exec_help(struct cnx_env *, char const *command);
int exec_noop(struct cnx_env *);
int exec_quit(struct cnx_env *);

// From parse.c

struct header;
struct msg_tree {
	struct header *header;
	enum msg_tree_type { CT_FILE, CT_SUBTREE } type;
	union {
		struct varbuf file;
		SLIST_HEAD(subtrees, msg_tree) subtree;
	} content;
	SLIST_ENTRY(msg_tree) entry;
};

struct msg_tree;
int msg_tree_read(struct msg_tree **root, int fd);
void msg_tree_del(struct msg_tree *);

#endif
