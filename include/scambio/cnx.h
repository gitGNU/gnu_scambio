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

/* These store the keywords usefull for the mdir protocol.
 * Keyword are compared by address so it's usefull to have all these here.
 */
extern char const kw_auth[];
extern char const kw_put[];
extern char const kw_rem[];
extern char const kw_sub[];
extern char const kw_unsub[];
extern char const kw_quit[];
extern char const kw_patch[];
extern char const kw_creat[];
extern char const kw_down[];
extern char const kw_up[];
extern char const kw_copy[];
extern char const kw_skip[];

/* Struct mdir_cnx describe a connection following mdir protocol.
 * Client and server are assymetric but similar.
 */

/* A structure associated with each sent query.
 * You can (should) inherit it to add your usefull infos.
 */

struct mdir_sent_query {
	LIST_ENTRY(mdir_sent_query) cnx_entry;
	long long seq;
};

/* TODO: - a callback to free a sent_query ?
 *       - a callback to report a sent_query timeout ?
 */
struct mdir_cnx {
	int fd;
	long long next_seq;
	struct mdir_user *user;
	struct mdir_syntax *syntax;
	LIST_HEAD(sent_queries, mdir_sent_query) sent_queries;
};

/* Connect to given host:port (send auth if username is given)
 */
void mdir_cnx_ctor_outbound(struct mdir_cnx *cnx, struct mdir_syntax *syntax, char const *host, char const *service, char const *username);

/* After you accepted the connection.
 * No auth is performed (user set to NULL).
 */
void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, struct mdir_syntax *syntax, int fd);

/* Delete a cnx object.
 * Notice that the connection will not necessarily be closed at once,
 * but may be kept for future mdir_cnx_new(). (TODO)
 */
void mdir_cnx_dtor(struct mdir_cnx *cnx);

/* Evaluates to a mdir_cmd_def for a query answer.
 */
#define MDIR_CNX_QUERY_REGISTER(KEYWORD, CALLBACK) { \
	.keyword = (KEYWORD), .cb = (CALLBACK), .nb_arg_min = 1, .nb_arg_max = UINT_MAX, \
	.nb_types = 1, .types = { CMD_INTEGER } \
}

/* Sends a query to the peer.
 * The query must have been registered first even if you do not expect an answer.
 * If !sq, no seqnum will be set (and you will receive no answer).
 * Otherwise it will be linked from the cnx for later retrieval (see mdir_cnx_query_retrieve()).
 */
void mdir_cnx_query(struct mdir_cnx *cnx, char const *kw, struct mdir_sent_query *sq, ...)
#ifdef __GNUC__
	__attribute__ ((sentinel))
#endif
;

/* Will use a mdir_parser build from all expected query responses, and by all
 * registered services.
 * It is not possible to confuse answers from commands, since commands are either
 * assymetric, or not acknowledged (the only symetrical commands, which are the
 * copy/skip data transfert commands, are not answered except for miss commands).
 * The mdir_cmd_cb of any matching definition will be called, with the cnx
 * as user_data. For query answers, use the retrieve function to retrieve your sent_query.
 */
void mdir_cnx_read(struct mdir_cnx *cnx);

/* Once in a query callback, you want to retrieve your sent_query, if for nothing
 * else then to delete it (the internal sent_query will be destructed).
 */
struct mdir_sent_query *mdir_cnx_query_retrieve(struct mdir_cnx *cnx, struct mdir_cmd *cmd);

/* Once in a service callback, you may want to answer a query.
 * If the seqnum is set it will be prepended.
 */
void mdir_cnx_answer(struct mdir_cnx *, struct mdir_cmd *, int status, char const *compl);

#endif
