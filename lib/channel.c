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
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/channel.h"
#include "scambio/cnx.h"
#include "misc.h"
#include "auth.h"

/*
 * Data Definitions
 */

static char const *mdir_files;
static struct mdir_syntax syntax;
static bool client;
static mdir_cmd_cb finalize_creat;

/*
 * Init
 */

void chn_end(void)
{
	mdir_syntax_dtor(&syntax);
}

void chn_begin(bool client_)
{
	client = client_;
	if_fail(conf_set_default_str("MDIR_FILES_DIR", "/tmp/mdir/files")) return;
	mdir_files = conf_get_str("MDIR_FILES_DIR");
	if_fail (mdir_syntax_ctor(&syntax)) return;
	static struct mdir_cmd_def def_client[] = {
		MDIR_CNX_QUERY_REGISTER(kw_creat, finalize_creat),
		MDIR_CNX_QUERY_REGISTER(kw_write, finalize_txstart),
		MDIR_CNX_QUERY_REGISTER(kw_read,  finalize_txstart),
	};
	static struct mdir_cmd_def def_server[] = {
		{
			.keyword = kw_creat, .cb = NULL,            .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {},
		}, {
			.keyword = kw_write, .cb = NULL,            .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_read,  .cb = NULL,            .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		},
	};
	static struct mdir_cmd_def def_common[] = {
		{
			.keyword = kw_copy,  .cb = NULL,            .nb_arg_min = 3, .nb_arg_max = 4,
			.nb_types = 3, .types = { CMD_INTEGER, CMD_INTEGER, CMD_INTEGER, CMD_STRING },	// seqnum of the read/write, offset, length, [eof]
		}, {
			.keyword = kw_skip,  .cb = NULL,            .nb_arg_min = 3, .nb_arg_max = 3,
			.nb_types = 2, .types = { CMD_INTEGER, CMD_INTEGER, CMD_INTEGER },	// seqnum of the read/write, offset, length
		}, {
			.keyword = kw_miss,  .cb = NULL,            .nb_arg_min = 3, .nb_arg_max = 3,
			.nb_types = 2, .types = { CMD_INTEGER, CMD_INTEGER, CMD_INTEGER },	// seqnum of the read/write, offset, length
		},
	};
	for (unsigned d=0; d<sizeof_array(def_common); d++) {
		if_fail (mdir_syntax_register(&syntax, def_common+d)) {
			mdir_syntax_dtor(&syntax);
			return;
		}
	}
	if (client) for (unsigned d=0; d<sizeof_array(def_client); d++) {
		if_fail (mdir_syntax_register(&syntax, def_client+d)) {
			mdir_syntax_dtor(&syntax);
			return;
		}
	} else for (unsigned d=0; d<sizeof_array(def_server); d++) {
		if_fail (mdir_syntax_register(&syntax, def_server+d)) {
			mdir_syntax_dtor(&syntax);
			return;
		}
	}
}

/*
 * Low level API
 */

/*
 * Boxes
 */

extern inline struct chn_box *chn_box_alloc(size_t bytes);
extern inline void chn_box_free(struct chn_box *box);
extern inline void chn_box_ref(struct chn_box *box);
extern inline void chn_box_unref(struct chn_box *box);
extern inline void *chn_box_unbox(struct chn_box *box);

/*
 * WTX
 */

struct chn_wtx {
	struct mdir_cnx *cnx;
	char const *name;	// strduped
	long long id;
	// for the client only :
	struct mdir_sent_query start_sq;	// the query that started the TX
	bool completed;	// when we receive the answer from the server
};

static void chn_wtx_ctor(struct chn_wtx *wtx, struct mdir_cnx *cnx, char const *name, long long id)
{
	wtx->cnx = cnx;
	wtx->name = strdup(name);
	if (! wtx->name) with_error(ENOMEM, "Cannot strdup name") return;
	if (client) {	// the client send a query first (_AFTER_ the TX is complete it will receive an answer from the server)
		wtx->tx_mode = false;
		if_fail (mdir_cnx_query(cnx, kw_write, &wtx->write_sq, name, NULL)) {
			free(wtx->name);
			return;
		}
		wtx->id = wtx->write_sq.seq;
	} else {
		wtx->tx_mode = true;
		wtx->id = id;	// from received command
	}
	// TODO: start reader thread
}

static void chn_wtx_dtor(struct chn_wtx *wtx)
{
	free(wtx->name);
	if (! wtx->tx_mode) mdir_cnx_query_cancel(wtx->cnx, &wtx->write_sq);
}

struct chn_wtx *chn_wtx_new(struct mdir_cnx *cnx, char const *name, long long id)
{
	struct chn_wtx *wtx = malloc(sizeof(*wtx));
	if (! wtx) with_error(ENOMEM, "malloc(wtx)") return NULL;
	if_fail (chn_wtx_ctor(wtx, cnx, name, id)) {
		free(wtx);
		wtx = NULL;
	}
	return wtx;
}

void chn_wtx_del(struct chn_wtx *wtx)
{
	chn_wtx_dtor(wtx);
	free(wtx);
}

void chn_wtx_write(struct chn_wtx *tx, off_t offset, size_t length, struct chn_box *box, bool eof)
{
}

/*
 * RTX
 */

struct chn_rtx {
};

struct chn_rtx *chn_rtx_new(struct mdir_cnx *, char const *name)
{
}

void chn_rtx_del(struct chn_rtx *tx)
{
}

struct chn_box *chn_rtx_read(struct chn_rtx *tx, off_t *offset, size_t *length, bool *eof)
{
}

bool chn_rtx_complete(struct chn_rtx *tx)
{
}

bool chn_rtx_should_wait(struct chn_rtx *tx)
{
}

/*
 * High level API for mere files
 */

/*
 * Get a file from cache (download it first if necessary)
 */

static void fetch_file_with_cnx(struct mdir_cnx *cnx, char const *name, int fd)
{
	struct chn_rtx *tx;
	if_fail (tx = chn_rtx_new(cnx, name)) return;
	bool eof_received = false;
	do {
		struct chn_box *box;
		off_t offset;
		size_t length;
		bool eof;
		if_fail (box = chn_rtx_read(tx, &offset, &length, &eof)) break;
		WriteTo(fd, offset, box->data, length);
		chn_box_unref(box);
		on_error break;
		if (eof) eof_received = true;
	} while (chn_rtx_should_wait(tx));
	if (! chn_rtx_complete(tx)) error_push(0, "Transfert incomplete");
	chn_rtx_del(tx);
}

int chn_get_file(char *localfile, size_t len, char const *name, char const *host, char const *service, char const *username)
{
	assert(localfile && name);
	// Look into the file cache
	int actual_len = snprintf(localfile, len, "%s/%s", mdir_files, name);
	if (actual_len >= (int)len) with_error(0, "buffer too short (need %d)", actual_len) return actual_len;
	int fd = open(localfile, O_RDONLY);
	if (fd < 0) {
		// TODO: Touch it
		(void)close(fd);
		return actual_len;
	}
	if (errno != ENOENT) with_error(errno, "Cannot open(%s)", localfile) return actual_len;
	// So lets fetch it
	fd = open(localfile, O_RDWR|O_CREAT);
	if (fd < 0) with_error(errno, "Cannot create(%s)", localfile) return actual_len;
	struct mdir_cnx cnx;
	if_fail (mdir_cnx_ctor_outbound(&cnx, &syntax, host, service, username)) return actual_len;
	fetch_file_with_cnx(&cnx, name, fd);
	on_error {
		// delete the file if the transfert failed for some reason
		if (0 != unlink(localfile)) error_push(errno, "Failed to download %s, but cannot unlink it", localfile);
	}
	mdir_cnx_dtor(&cnx);
	return actual_len;
}

/*
 * Request a new file name
 */

struct creat_param {
	char *name;
	size_t len;
	bool done;
	struct mdir_sent_query sq;
};

static void finalize_creat(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	assert(cmd->def->keyword == kw_creat);
	struct mdir_sent_query *sq = mdir_cnx_query_retrieve(cnx, cmd);
	struct creat_param *param = DOWNCAST(sq, sq, creat_param);
	assert(! param->done);
	if (cmd->args[0].integer != 200) return;
	snprintf(param->name, param->len, "%s", cmd->args[1].string);
	param->done = true;
}

void chn_create(char *name, size_t len, bool rt, char const *host, char const *service, char const *username)
{
	assert(client);
	struct mdir_cnx cnx;
	if_fail (mdir_cnx_ctor_outbound(&cnx, &syntax, host, service, username)) return;
	do {
		struct creat_param param = { .name = name, .len = len, .done = false, };
		if_fail (mdir_cnx_query(&cnx, kw_creat, &param.sq, rt ? "*":NULL, NULL)) break;
		if_fail (mdir_cnx_read(&cnx)) break;	// wait until all queries are answered or timeouted
		if (! param.done) with_error(0, "Cannot create new file") break;
	} while (0);
	mdir_cnx_dtor(&cnx);
}

/*
 * Send a local file to a channel
 */

static void send_file_with_cnx(struct mdir_cnx *cnx, char const *name, int fd)
{
	off_t offset = 0, max_offset;
	if_fail (max_offset = filesize(fd)) return;
	struct chn_wtx *tx;
	if_fail (tx = chn_wtx_new(cnx, name)) return;
#	define READ_FILE_BLOCK 65500
	do {
		struct chn_box *box;
		bool eof = true;
		size_t len = max_offset - offset;
		if (len > READ_FILE_BLOCK) {
			len = READ_FILE_BLOCK;
			eof = false;
		}
		if_fail (box = chn_box_alloc(len)) break;
		ReadFrom(box->data, fd, offset, len);
		unless_error chn_wtx_write(tx, offset, len, box, eof);
		offset += len;
		on_error break;
	} while (offset < max_offset);
#	undef READ_FILE_BLOCK
	chn_wtx_del(tx);
}

void chn_send_file(char const *name, int fd, char const *host, char const *service, char const *username)
{
	struct mdir_cnx cnx;
	if_fail (mdir_cnx_ctor_outbound(&cnx, &syntax, host, service, username)) return;
	send_file_with_cnx(&cnx, name, fd);
	mdir_cnx_dtor(&cnx);
}

