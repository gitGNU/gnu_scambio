/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
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
 * Server
 */

// TODO: use getaddrinfo(3)
void server_ctor(struct server *serv, unsigned short port)
{
	int const one = 1;
	struct sockaddr_in any_addr;
	memset(&any_addr, sizeof(any_addr), 0);
	any_addr.sin_family = AF_INET;
	any_addr.sin_port = htons(port);
	any_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv->sock_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (serv->sock_fd == -1) {
		error_push(errno, "Cannot create socket");
	} else if (
		0 != setsockopt(serv->sock_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
		0 != bind(serv->sock_fd, (struct sockaddr *)&any_addr, sizeof(any_addr)) ||
		0 != listen(serv->sock_fd, 10)
	) {
		error_push(errno, "Cannot use socket");
		(void)close(serv->sock_fd);
		serv->sock_fd = -1;
	}
}

void server_dtor(struct server *serv)
{
	if (serv->sock_fd >= 0) {
		(void)close(serv->sock_fd);
		serv->sock_fd = -1;
	}
}

int server_accept(struct server *serv)
{
	return pth_accept(serv->sock_fd, &serv->last_accepted_addr, &serv->last_accepted_addr_len);
}

