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

/* Struct mdir_cnx describe a connection to the chnd server (from a plugin or from
 * chnd itself), with socket, user name and key used.
 * Clients (plugins) and server (channeld) are assymetric here.
 */
struct mdir_cnx;

/* Connect to MDIRD_HOST:MDIRD_PORT and send auth.
 */
struct mdir_cnx *mdir_cnx_new_outbound(char const *host, char const *service, char const *username);
void mdir_cnx_ctor_outbound(struct mdir_cnx *cnx, char const host, char const *service, char const *username);

/* Accept and set user to NULL
 */
struct mdir_cnx *mdir_cnx_new_inbound(int fd);
void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, int fd);

/* Then set the user for the connection
 */
void mdir_cnx_set_user(struct mdir_cnx *cnx, char const *username);

/* Delete a cnx object.
 * Notice that the connection will not necessarily be closed at once,
 * but may be kept for future mdir_cnx_new().
 */
void mdir_cnx_del(struct mdir_cnx *cnx);
void mdir_cnx_dtor(struct mdir_cnx *cnx);

/* Sends a query to the peer.
 * set the callback to NULL to indicate that no answer is expected, and then
 * no seqnum will be set. Otherwise a seqnum will be chosen in sequence (<0 if you
 * are server side).
 * Otherwise, the cb will be called when the answer is received, while in mdir_cnx_read().
 */
typedef void mdir_cnx_answ_cb(char const *kw, int status, char const *compl, void *user_data);
void mdir_cnx_query(struct mdir_cnx *cnx, mdir_cnx_answ_cb *cb, void *user_data, char const *kw, ...);

/* parser and cmd may be NULL if you are only interrested in answers.
 */
void mdir_cnx_read(struct mdir_cnx *cnx, struct mdir_cmd *cmd, struct mdir_parser *parser);

/* Once in a server callback, you may want to answer a query.
 * If the seqnum is 0 the answer is ignored (you may as well not call this function then).
 */
void mdir_cnx_answer(struct mdir_cnx *, struct mdir_cmd *, int status, char const *compl);

#endif