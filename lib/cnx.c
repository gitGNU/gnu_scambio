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
#include <string.h>
#include <unistd.h>
#include "scambio.h"
#include "scambio/cnx.h"
#include "scambio/header.h"
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
char const kw_thx[]   = "thx";

/*
 * Constructors for mdir_sent_query
 */

extern inline void mdir_sent_query_ctor(struct mdir_sent_query *sq);
extern inline void mdir_sent_query_dtor(struct mdir_sent_query *sq);

/*
 * Register/Send a query
 */

struct mdir_sent_query *mdir_cnx_query_retrieve(struct mdir_cnx *cnx, struct mdir_cmd *cmd)
{
	time_t now = time(NULL);
	// look for the mdir_sent_query
	struct mdir_sent_query *sq, *tmp;
	LIST_FOREACH_SAFE(sq, &cnx->sent_queries, cnx_entry, tmp) {
		if (sq->seq == cmd->seq) {
			// FIXME : call the user provided del function (for instance, chn commands include a send_query and should be freed now)
			mdir_sent_query_dtor(sq);
			return sq;
		}
#		define SQ_TIMEOUT 20
		if (sq->creation + SQ_TIMEOUT < now) {
			warning("Timeouting query which seqnum=%lld (created at TS=%lu)", sq->seq, sq->creation);
			mdir_sent_query_dtor(sq);	// FIXME : same as above
		}
	}
	with_error(0, "Unexpected answer for seq# %lld", cmd->seq) return NULL;
}

void mdir_cnx_query(struct mdir_cnx *cnx, char const *kw, struct header *h, struct mdir_sent_query *sq, ...)
{
	if (cnx->fd == -1 || (!cnx->authed && kw != kw_auth)) with_error(0, "cnx not useable yet") return;
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
		if (h) if_fail (header_dump(h, &vb)) break;
		debug("Will write '%s'", vb.buf);
		if_fail (Write(cnx->fd, vb.buf, vb.used)) break;
		if (sq) {
			sq->seq = seq;
			sq->creation = time(NULL);
			LIST_INSERT_HEAD(&cnx->sent_queries, sq, cnx_entry);
		}
	} while (0);
	varbuf_dtor(&vb);
}

/*
 * Constructors for mdir_cnx
 */

static void cnx_ctor_common(struct mdir_cnx *cnx, struct mdir_syntax *syntax, bool client)
{
	cnx->client = client;
	cnx->authed = !client;	// server does not need to auth
	cnx->fd = -1;
	cnx->username = NULL;
	cnx->user = NULL;
	cnx->next_seq = 1;
	cnx->syntax = syntax;
	LIST_INIT(&cnx->sent_queries);
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

static void *connecter_thread(void *arg)
{
	struct mdir_cnx *cnx = (struct mdir_cnx *)arg;
	debug("New connecter thread for cnx@%p", cnx);
	while (1) {
		if (cnx->fd == -1) {
			// Try to connect untill success
			do {
				int fd;
				pth_yield(NULL);
				if_succeed (fd = Connect(cnx->host, cnx->service)) {
					cnx->authed = false;
					cnx->fd = fd;
					break;
				}
				pth_sleep(15);
			} while (1);
			// Try to log in
			if (cnx->username) do {
				struct mdir_cmd_def auth_def = MDIR_CNX_ANSW_REGISTER(kw_auth, auth_answ);
				if_fail (mdir_syntax_register(cnx->syntax, &auth_def)) break;
				struct auth_sent_query my_sq = { .done = false, };
				if_fail (mdir_cnx_query(cnx, kw_auth, NULL, &my_sq.sq, cnx->username, NULL)) break;
				if_fail (mdir_cmd_read(cnx->syntax, cnx->fd, cnx)) break;
				if_fail (mdir_syntax_unregister(cnx->syntax, &auth_def)) break;
				if (! my_sq.done) with_error (0, "no answer to auth") break;
				cnx->user = mdir_user_load(cnx->username);
			} while (0);
			on_error {	// If fail, close the connection and retry later
				error_clear();
				(void)close(cnx->fd);
				cnx->fd = -1;
				pth_sleep(30);
			} else {
				cnx->authed = true;	// If OK, allow other threads to use this cnx
			}
		} else {	// If the fd is OK, just wait until someone trash it
			pth_sleep(10);
		}
	}
	return NULL;
}

void mdir_cnx_ctor_outbound(struct mdir_cnx *cnx, struct mdir_syntax *syntax, char const *host, char const *service, char const *username)
{
	cnx_ctor_common(cnx, syntax, true);
	cnx->username = username;
	cnx->host = host;
	cnx->service = service;
	cnx->connecter_thread = pth_spawn(PTH_ATTR_DEFAULT, connecter_thread, cnx);
	if (! cnx->connecter_thread) with_error(0, "Cannot spawn connecter thread") return;
}

void mdir_cnx_ctor_inbound(struct mdir_cnx *cnx, struct mdir_syntax *syntax, int fd)
{
	cnx_ctor_common(cnx, syntax, false);
	cnx->fd = fd;
}

void mdir_cnx_dtor(struct mdir_cnx *cnx)
{
	debug("destruct cnx @%p", cnx);
	if (cnx->connecter_thread) {
		(void)pth_abort(cnx->connecter_thread);
		cnx->connecter_thread = NULL;
	}
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
	while (cnx->fd == -1 || !cnx->authed) {
		pth_sleep(10);
	}
	// New command will be handled by the user registered callback.
	// Query answers will be handled by our answer callback, which will then
	// know that the cmd->def is not merely a mdir_cmd_def but a query_def,
	// where it can look for the dedicated callback.
	mdir_cmd_read(cnx->syntax, cnx->fd, cnx);
}

void mdir_cnx_answer(struct mdir_cnx *cnx, struct mdir_cmd *cmd, int status, char const *compl)
{
	debug("status = %d, compl = %s, seq = %lld", status, compl, cmd->seq);
	assert(cnx->fd != -1);
	char reply[512];
	size_t len = 0;
	if (cmd->seq != 0) {
		assert(cmd->seq > 0);
		len += snprintf(reply, sizeof(reply), "-%lld ", cmd->seq);
	} else if (cnx->syntax->no_answer_if_no_seqnum) return;
	len += snprintf(reply+len, sizeof(reply)-len, "%s %d %s\n", cmd->def->keyword, status, compl);
	Write(cnx->fd, reply, len);
}

