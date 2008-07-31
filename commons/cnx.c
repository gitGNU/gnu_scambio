#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pth.h>
#include "scambio.h"
#include "cnx.h"

/*
 * (De)Initialization.
 */

int cnx_begin(void)
{
	return 0;
}

void cnx_end(void)
{
}

/*
 * Server
 */

// TODO: use getaddrinfo(3)
int cnx_server_ctor(struct cnx_server *serv, unsigned short port)
{
	int err = 0;
	int const one = 1;
	struct sockaddr_in any_addr;
	memset(&any_addr, sizeof(any_addr), 0);
	any_addr.sin_family = AF_INET;
	any_addr.sin_port = htons(port);
	any_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv->sock_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (serv->sock_fd == -1) {
		err = -errno;
	} else if (
		0 != setsockopt(serv->sock_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
		0 != bind(serv->sock_fd, (struct sockaddr *)&any_addr, sizeof(any_addr)) ||
		0 != listen(serv->sock_fd, 10)
	) {
		err = -errno;
		(void)close(serv->sock_fd);
		serv->sock_fd = -1;
	}
	return err;
}

void cnx_server_dtor(struct cnx_server *serv)
{
	if (serv->sock_fd >= 0) {
		(void)close(serv->sock_fd);
		serv->sock_fd = -1;
	}
}

int cnx_server_accept(struct cnx_server *serv)
{
	return pth_accept(serv->sock_fd, &serv->last_accepted_addr, &serv->last_accepted_addr_len);
}

/*
 * Client
 */

#include <netdb.h>
static int gaierr2errno(int err)
{
	switch (err) {
		case EAI_SYSTEM: return errno;
		case EAI_MEMORY: return ENOMEM;
	}
	return -1;	// FIXME
}

int cnx_client_ctor(struct cnx_client *cnx, char const *host, char const *service)
{
	// Resolve hostname into sockaddr
	struct addrinfo *info_head, *info;
	int err;
	if (0 != (err = getaddrinfo(host, service, NULL, &info_head))) {
		// TODO: check that freeaddrinfo is not required in this case
		error("Cannot getaddrinfo : %s", gai_strerror(err));
		return -gaierr2errno(err);
	}

	int last_err = -1;
	for (info = info_head; info; info = info->ai_next) {
		cnx->sock_fd = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (cnx->sock_fd == -1) continue;
		if (0 == connect(cnx->sock_fd, info->ai_addr, info->ai_addrlen)) break;
		last_err = -errno;
		close(cnx->sock_fd);
		cnx->sock_fd = -1;
	}
	if (! info) {
		error("No suitable address found for host '%s'", host);
		return last_err;
	}
	freeaddrinfo(info_head);
	return 0;
}

void cnx_client_dtor(struct cnx_client *cnx)
{
	if (cnx->sock_fd >= 0) {
		(void)close(cnx->sock_fd);
		cnx->sock_fd = -1;
	}
}
