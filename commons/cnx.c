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
 * Data Definitions
 */

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

int cnx_server_ctor(struct cnx_server *serv, unsigned short port)
{
	int err = 0;
	static struct sockaddr_in any_addr;
	memset(&any_addr, sizeof(any_addr), 0);
	any_addr.sin_family = AF_INET;
	any_addr.sin_port = htons(port);
	any_addr.sin_addr.s_addr = INADDR_ANY;
	serv->sock_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (serv->sock_fd == -1) {
		err = -errno;
	} else if (
		0 != bind(serv->sock_fd, &any_addr, sizeof(any_addr)) ||
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

