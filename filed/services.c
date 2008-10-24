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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include "scambio.h"
#include "filed.h"
#include "stream.h"

/*
 * My TX
 */

static void tx_ctor(struct my_tx *mtx, struct chn_cnx *cnx, struct stream *stream, long long id, bool reader)
{
	mtx->stream = stream;
	if (reader) {	// If I read the stream, Im a sender
		if_fail (chn_tx_ctor_sender(&mtx->tx, cnx, NULL, id)) return;
		if_fail (stream_add_reader(stream, mtx)) {
			error_save();
			chn_tx_dtor(&mtx->tx);
			error_restore();
			return;
		}
	} else {
		if_fail (chn_tx_ctor_receiver(&mtx->tx, cnx, NULL, id)) return;
	}
}

static struct my_tx *tx_new(struct chn_cnx *cnx, struct stream *stream, long long id, bool reader)
{
	struct my_tx *mtx = malloc(sizeof(*mtx));
	if (! mtx) with_error(ENOMEM, "malloc(my_tx)") return NULL;
	if_fail (tx_ctor(mtx, cnx, stream, id, reader)) {
		free(mtx);
		mtx = NULL;
	}
	return mtx;
}

/*
 * Creation
 */

void serve_creat(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	// Parameter
	bool rt = cmd->nb_args == 1 && 0 == strcmp("*", cmd->args[0].string);
	if (! rt && cmd->nb_args != 0) {
		mdir_cnx_answer(cnx, cmd, 500 /* SYNTAX ERROR */, "Bad syntax");
		return;
	}
	// Just create a unique file (if it's a file), or create the RT stream,
	char path[PATH_MAX];
	char *name;
	static unsigned rtseq = 0;
	if (rt) {
		snprintf(path, sizeof(path), "%u_%u", (unsigned)time(NULL), rtseq++);
		name = path;
		(void)stream_new_rt(name);	// we drop the ref, the RT timeouter will unref if
	} else {
		// TODO a per day/week directory would be better
		int len = snprintf(path, sizeof(path), "%s/XXXXXX", files_root);
		name = path + len - 6;
		int fd = mkstemp(path);
		if (fd < 0) {
			error("Cannot mkstemp(%s) : %s", path, strerror(errno));
			mdir_cnx_answer(cnx, cmd, 501 /* INTERNAL ERROR */, "Cannot mkstemp");
			return;
		}
		(void)close(fd);
	}
	// and answer at once.
	mdir_cnx_answer(cnx, cmd, 200, name);
}

/*
 * Write
 */

void incoming(struct chn_cnx *cnx, struct chn_tx *tx, off_t offset, size_t size, struct chn_box *box, bool eof)
{
	debug("offset %u, size %u, eof = %c", (unsigned)offset, (unsigned)size, eof ? 'y':'n');
	(void)cnx;
	struct my_tx *mtx = DOWNCAST(tx, tx, my_tx);
	assert(mtx->stream);
	stream_write(mtx->stream, offset, size, box, eof);
}

void serve_write(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	struct chn_cnx *ccnx = DOWNCAST(cnx, cnx, chn_cnx);
	char const *name = cmd->args[0].string;
	struct stream *stream;
	
	if (cmd->seq == -1) {
		mdir_cnx_answer(cnx, cmd, 500, "Missing seqnum");
		return;
	}
	if_fail (stream = stream_lookup(name)) {
		error_clear();
		mdir_cnx_answer(cnx, cmd, 500, error_str());
		return;
	}
	if_fail ((void)tx_new(ccnx, stream, cmd->seq, false)) {
		error_clear();
		stream_unref(stream);
		mdir_cnx_answer(cnx, cmd, 500, error_str());
		return;
	}
}

