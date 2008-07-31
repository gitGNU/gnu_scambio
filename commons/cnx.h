#ifndef CNX_H_080622
#define CNX_H_080622

#include <sys/socket.h>

struct cnx_server {
	int sock_fd;
	// Following two are filled by cnx_server_accept
	struct sockaddr last_accepted_addr;
	socklen_t last_accepted_addr_len;
};

struct cnx_client {
	int sock_fd;
};

int cnx_begin(void);
void cnx_end(void);
int cnx_server_ctor(struct cnx_server *, unsigned short port);
void cnx_server_dtor(struct cnx_server *);
int cnx_server_accept(struct cnx_server *);
int cnx_client_ctor(struct cnx_client *, char const *host, char const *service);
void cnx_client_dtor(struct cnx_client *);

#endif
