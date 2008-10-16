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
#include "misc.h"
#include "auth.h"

/*
 * Data Definitions
 */

static char const *mdir_files;
static char const *kw_creat = "creat";

/*
 * Init
 */

void chn_end(void)
{
}

void chn_begin(void)
{
	if_fail(conf_set_default_str("MDIR_FILES_DIR", "/tmp/mdir/files")) return;
	mdir_files = conf_get_str("MDIR_FILES_DIR");
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
	} while (!chn_rtx_complete(tx) && chn_rtx_should_wait(tx));
	if (! chn_rtx_complete(tx)) error_push(0, "Transfert incomplete");
	chn_rtx_del(tx);
}

int chn_get_file(char *localfile, size_t len, char const *name, char const *username)
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
	struct mdir_cnx *cnx = mdir_cnx_new_outbound(username);
	on_error return actual_len;
	fetch_file_with_cnx(cnx, name, fd);
	on_error {
		// delete the file if the transfert failed for some reason
		if (0 != unlink(localfile)) error_push(errno, "Failed to download %s, but cannot unlink it", localfile);
	}
	mdir_cnx_del(cnx);
	return actual_len;
}

/*
 * Request a new file name
 */

struct creat_param {
	char *name;
	size_t len;
	bool done;
};

static void finalize_creat(struct mdir_cnx *cnx, char const *kw, int status, char const *compl, void *data)
{
	(void)cnx;
	assert(kw == kw_creat);
	struct creat_param *param = (struct creat_param *)data;
	assert(! param->done);
	if (status != 200) return;
	snprintf(param->name, param->len, "%s", compl);
	param->done = true;
}

void chn_create(char *name, size_t len, bool rt, char const *username)
{
	struct mdir_cnx *cnx;
	if_fail (cnx = mdir_cnx_new_outbound(username)) return;
	do {
		struct creat_param param = { .name = name, .len = len, .done = false };
		if_fail (mdir_cnx_query(cnx, finalize_creat, &param, kw_creat, rt ? "*":NULL, NULL)) break;
		if_fail (mdir_cnx_read(cnx, NULL)) break;	// wait until all queries are answered or timeouted
		if (! param.done) with_error(0, "Cannot create new file") break;
	} while (0);
	mdir_cnx_del(cnx);
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

void chn_send_file(char const *name, int fd, char const *username)
{
	struct mdir_cnx *cnx;
	if_fail (cnx = mdir_cnx_new_outbound(username)) return;
	send_file_with_cnx(cnx, name, fd);
	mdir_cnx_del(cnx);
}

