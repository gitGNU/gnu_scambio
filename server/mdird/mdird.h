#ifndef MDIRD_H_080623
#define MDIRD_H_080623

#include "queue.h"

struct subscription;
struct cnx_env {
	int fd;
	LIST_HEAD(subscriptions, subscription) subscriptions;
};

int exec_sub  (struct cnx_env *, long long seq, char const *dir, long long version);
int exec_unsub(struct cnx_env *, long long seq, char const *dir);
int exec_put  (struct cnx_env *, long long seq, char const *dir);
int exec_class(struct cnx_env *, long long seq, char const *dir);
int exec_rem  (struct cnx_env *, long long seq, char const *dir);

#endif
