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
#ifndef SERVER_H_080622
#define SERVER_H_080622

#include <sys/socket.h>

struct server {
	int sock_fd;
	// Following two are filled by server_accept
	struct sockaddr last_accepted_addr;
	socklen_t last_accepted_addr_len;
};

void server_ctor(struct server *, unsigned short port);
void server_dtor(struct server *);
int server_accept(struct server *);

#endif
