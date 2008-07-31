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
