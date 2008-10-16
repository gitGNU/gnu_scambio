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
#ifndef CNX_H_081016
#define CNX_H_081016
#include <scambio/cmd.h>

/* Struct mdir_cnx describe a connection following mdir protocol.
 * Client and server are assymetric but similar.
 */
struct sent_query;
struct mdir_cnx {
	int fd;
	struct mdir_user *user;
	struct mdir_syntax syntax;
	LIST_HEAD(sent_queries, sent_query) sent_queries;
};

/* Connect to MDIRD_HOST:MDIRD_PORT and send auth.
 */
void mdir_cnx_ctor_outbound(struct mdir_cnx *cnx, char const host, char const *service, char const *username);

/* After you accepted the connection.
 * Handle user auth.
 */
void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, int fd);

/* Delete a cnx object.
 * Notice that the connection will not necessarily be closed at once,
 * but may be kept for future mdir_cnx_new(). (TODO)
 */
void mdir_cnx_dtor(struct mdir_cnx *cnx);

/* Sends a query to the peer.
 * set the callback to NULL to indicate that no answer is expected, and then
 * no seqnum will be set. Otherwise a seqnum will be chosen in sequence (<0 if you
 * are server side).
 * Otherwise, the cb will be called when the answer is received, while in mdir_cnx_read().
 */
typedef void mdir_cnx_answ_cb(char const *kw, struct mdir_cmd *cmd, void *user_data);
void mdir_cnx_register_query(struct mdir_cnx *cnx, char const *keyword, mdir_cnx_answ_cb *cb);
void mdir_cnx_query(struct mdir_cnx *cnx, void *user_data, char const *kw, ...);

/* Register that a given command definition is handled by the given callback.
 * Will only call mdir_cmd_def_register for the cnx syntax
 */
typedef void mdir_cnx_serve_cb(struct mdir_cnx *cnx, struct mdir_cmd *cmd, void *user_data);
void mdir_cnx_register_service(struct mdir_cnx *cnx, struct mdir_cmd_def *def);

/* Will use a mdir_parser build from all expected query responses, and by all
 * registered services.
 * It is not possible to confuse answers from commands, since commands are either
 * assymetric, or not acknowledged (the only symetrical commands, which are the
 * copy/skip data transfert commands, are not answered except for miss commands).
 */
void mdir_cnx_read(struct mdir_cnx *cnx);

/* Once in a server callback, you may want to answer a query.
 * If the seqnum is 0 the answer is ignored (you may as well not call this function then).
 */
void mdir_cnx_answer(struct mdir_cnx *, struct mdir_cmd *, int status, char const *compl);

#endif
