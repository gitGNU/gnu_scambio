#ifndef MDIRD_H_080623
#define MDIRD_H_080623

#include <pth.h>
#include "queue.h"

struct subscription;
struct cnx_env {
	int fd;
	LIST_HEAD(subscriptions, subscription) subscriptions;
	pth_mutex_t wfd;	// protects fd on write
};

int exec_begin(void);
void exec_end(void);
int exec_sub  (struct cnx_env *, long long seq, char const *dir, long long version);
int exec_unsub(struct cnx_env *, long long seq, char const *dir);
int exec_put  (struct cnx_env *, long long seq, char const *dir);
int exec_class(struct cnx_env *, long long seq, char const *dir);
int exec_rem  (struct cnx_env *, long long seq, char const *dir);
int exec_quit (struct cnx_env *, long long seq);

#endif
