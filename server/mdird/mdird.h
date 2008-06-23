#ifndef MDIRD_H_080623
#define MDIRD_H_080623

struct cnx_env {
	int fd;
};

int exec_diff (struct cnx_env *, long long seq, char const *dir, long long version);
int exec_put  (struct cnx_env *, long long seq, char const *dir);
int exec_class(struct cnx_env *, long long seq, char const *dir);
int exec_rem  (struct cnx_env *, long long seq, char const *dir);

#endif
