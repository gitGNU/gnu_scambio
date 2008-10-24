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
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <netdb.h>
#include "scambio.h"
#include "scambio/cnx.h"
#include "varbuf.h"
#include "misc.h"
#include "auth.h"

/*
 * Data Definitions
 */

char const kw_auth[]  = "auth";
char const kw_put[]   = "put";
char const kw_rem[]   = "rem";
char const kw_sub[]   = "sub";
char const kw_unsub[] = "unsub";
char const kw_quit[]  = "quit";
char const kw_patch[] = "patch";
char const kw_creat[] = "creat";
char const kw_write[] = "write";
char const kw_read[]  = "read";
char const kw_copy[]  = "copy";
char const kw_skip[]  = "skip";
char const kw_miss[]  = "miss";

/*
 * Constructors for mdir_sent_query
 */

static void mdir_sent_query_ctor(struct mdir_sent_query *sq, struct mdir_cnx *cnx, long long seq)
{
	sq->seq = seq;
	LIST_INSERT_HEAD(&cnx->sent_queries, sq, cnx_entry);
}

static void mdir_sent_query_dtor(struct mdir_sent_query *sq)
{
	LIST_REMOVE(sq, cnx_entry);
}

/*
 * Register/Send a query
 */

struct mdir_sent_query *mdir_cnx_query_retrieve(struct mdir_cnx *cnx, struct mdir_cmd *cmd)
{
	// look for the mdir_sent_query
	struct mdir_sent_query *sq;
	LIST_FOREACH(sq, &cnx->sent_queries, cnx_entry) {
		if (sq->seq == cmd->seq) {
			mdir_sent_query_dtor(sq);
			return sq;
		}
	}
	with_error(0, "Unexpected answer for seq# %lld", cmd->seq) return NULL;
}

void mdir_cnx_query_cancel(struct mdir_cnx *cnx, struct mdir_sent_query *sq)
{
	(void)cnx;
	mdir_sent_query_dtor(sq);
}

void mdir_cnx_query(struct mdir_cnx *cnx, char const *kw, struct mdir_sent_query *sq, ...)
{
	struct varbuf vb;
	varbuf_ctor(&vb, 1024, true);
	long long seq = cnx->next_seq++;
	do {
		if (sq) {
			char buf[SEQ_BUF_LEN];
			if_fail (varbuf_append_strs(&vb, mdir_cmd_seq2str(buf, seq), " ", NULL)) break;
		}
		if_fail (varbuf_append_strs(&vb, kw, NULL)) break;
		va_list ap;
		va_start(ap, sq);
		char const *param;
		while (NULL != (param = va_arg(ap, char const *))) {
			if_fail (varbuf_append_strs(&vb, " ", param, NULL)) break;
		}
		va_end(ap);
		on_error break;
		if_fail (varbuf_append_strs(&vb, "\n", NULL)) break;
		debug("Will write '%s'", vb.buf);
		if_fail (Write(cnx->fd, vb.buf, vb.used-1)) break;	// do not output the final '\0'
		if_fail (mdir_sent_query_ctor(sq, cnx, seq)) break;
	} while (0);
	varbuf_dtor(&vb);
}

/*
 * Constructors for mdir_cnx
 */

static void cnx_ctor_common(struct mdir_cnx *cnx, struct mdir_syntax *syntax, bool client)
{
	cnx->client = client;
	cnx->fd = -1;
	cnx->user = NULL;
	cnx->next_seq = 0;
	cnx->syntax = syntax;
	LIST_INIT(&cnx->sent_queries);
}

static int gaierr2errno(int err)
{
	switch (err) {
		case EAI_SYSTEM: return errno;
		case EAI_MEMORY: return ENOMEM;
	}
	return -1;	// FIXME
}

static void cnx_connect(struct mdir_cnx *cnx, char const *host, char const *service)
{
	// Resolve hostname into sockaddr
	struct addrinfo *info_head, *ainfo;
	int err;
	if (0 != (err = getaddrinfo(host, service, NULL, &info_head))) {
		// TODO: check that freeaddrinfo is not required in this case
		with_error(gaierr2errno(err), "Cannot getaddrinfo") return;
	}
	err = ENOENT;
	for (ainfo = info_head; ainfo; ainfo = ainfo->ai_next) {
		cnx->fd = socket(ainfo->ai_family, ainfo->ai_socktype, ainfo->ai_protocol);
		if (cnx->fd == -1) continue;
		if (0 == connect(cnx->fd, ainfo->ai_addr, ainfo->ai_addrlen)) {
			info("Connected to %s:%s", host, service);
			break;
		}
		err = errno;
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
	if (! ainfo) error_push(err, "No suitable address found for host %s:%s", host, service);
	freeaddrinfo(info_head);
}

struct auth_sent_query {
	struct mdir_sent_query sq;
	bool done;
};

static void auth_answ(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	struct mdir_sent_query *sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct auth_sent_query *my_sq = DOWNCAST(sq, sq, auth_sent_query);
	if (cmd->args[0].integer == 200) my_sq->done = true;
}

void mdir_cnx_ctor_outbound(struct mdir_cnx *cnx, struct mdir_syntax *syntax, char const *host, char const *service, char const *username)
{
	cnx_ctor_common(cnx, syntax, true);
	if_fail (cnx_connect(cnx, host, service)) return;
	cnx->user = NULL;
	if (username) do {
		struct mdir_cmd_def auth_def = MDIR_CNX_QUERY_REGISTER(kw_auth, auth_answ);
		if_fail (mdir_syntax_register(cnx->syntax, &auth_def)) break;
		struct auth_sent_query my_sq = { .done = false, };
		if_fail (mdir_cnx_query(cnx, kw_auth, &my_sq.sq, username, NULL)) break;
		if_fail (mdir_cnx_read(cnx)) break;
		if_fail (mdir_syntax_unregister(cnx->syntax, &auth_def)) break;
		if (! my_sq.done) with_error (0, "no answer to auth") break;
		cnx->user = mdir_user_load(username);
	} while (0);
	on_error {
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
}

void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, struct mdir_syntax *syntax, int fd)
{
	cnx_ctor_common(cnx, syntax, false);
	cnx->user = NULL;
	cnx->fd = fd;
}

void mdir_cnx_dtor(struct mdir_cnx *cnx)
{
	debug("destruct cnx");
	if (cnx->fd != -1) {
		(void)close(cnx->fd);
		cnx->fd = -1;
	}
	// free pending sent_queries and call
	struct mdir_sent_query *sq;
	while (NULL != (sq = LIST_FIRST(&cnx->sent_queries))) {
		// TODO: use a function supplied by the user to free the sq ? Also, use another one (or the same) to report timeouts ??
		mdir_sent_query_dtor(sq);
	}
}

/*
 * Read
 */

void mdir_cnx_read(struct mdir_cnx *cnx)
{
	// New command will be handled by the user registered callback.
	// Query answers will be handled by our answer callback, which will then
	// now that the cmd->def is not merely a mdir_cmd_def but a query_def,
	// where it can look for the dedicated callback.
	mdir_cmd_read(cnx->syntax, cnx->fd, cnx);
}

void mdir_cnx_answer(struct mdir_cnx *cnx, struct mdir_cmd *cmd, int status, char const *compl)
{
	char reply[512];
	size_t len = 0;
	if (cmd->seq != -1) {
		len += snprintf(reply, sizeof(reply), "%lld ", cmd->seq);
	}
	len += snprintf(reply+len, sizeof(reply)-len, "%s %d %s\n", cmd->def->keyword, status, compl);
	Write(cnx->fd, reply, len);
}

