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
#include <string.h>
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
#include "stream.h"

/*
 * Data Definitions
 */

static char const *mdir_files;
static struct mdir_syntax syntax;
static bool server;
static mdir_cmd_cb finalize_creat, finalize_txstart, serve_copy, serve_skip, serve_miss, serve_creat, serve_read, serve_write, serve_quit;

#define RETRANSM_TIMEOUT 2000000//400000	// .4s
#define OUT_FRAGS_TIMEOUT 1000000	// timeout fragments after 1 second
#define CHUNK_SIZE 1400

/*
 * Init
 */

void chn_end(void)
{
	stream_end();
	mdir_syntax_dtor(&syntax);
}

void chn_begin(bool server_)
{
	server = server_;
	if_fail (stream_begin()) return;
	if_fail(conf_set_default_str("SCAMBIO_FILES_DIR", "/tmp/mdir/files")) return;
	mdir_files = conf_get_str("SCAMBIO_FILES_DIR");
	if_fail (mdir_syntax_ctor(&syntax)) return;
	static struct mdir_cmd_def def_server[] = {
		{
			.keyword = kw_creat, .cb = serve_creat, .nb_arg_min = 0, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_write, .cb = serve_write, .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_read,  .cb = serve_read,  .nb_arg_min = 1, .nb_arg_max = 1,
			.nb_types = 1, .types = { CMD_STRING },
		}, {
			.keyword = kw_quit,  .cb = serve_quit,  .nb_arg_min = 0, .nb_arg_max = 0,
			.nb_types = 0, .types = {},
		},
	};
	static struct mdir_cmd_def def_client[] = {
		MDIR_CNX_QUERY_REGISTER(kw_creat, finalize_creat),
		MDIR_CNX_QUERY_REGISTER(kw_write, finalize_txstart),
		MDIR_CNX_QUERY_REGISTER(kw_read,  finalize_txstart),
	};
	static struct mdir_cmd_def def_common[] = {
		{
			.keyword = kw_copy,  .cb = serve_copy, .nb_arg_min = 3, .nb_arg_max = 4,
			.nb_types = 3, .types = { CMD_INTEGER, CMD_INTEGER, CMD_INTEGER, CMD_STRING },	/* seqnum of the read/write, offset, length, [eof] */
		}, {
			.keyword = kw_skip,  .cb = serve_skip, .nb_arg_min = 3, .nb_arg_max = 4,
			.nb_types = 2, .types = { CMD_INTEGER, CMD_INTEGER, CMD_INTEGER },	/* seqnum of the read/write, offset, length, [eof] */
		}, {
			.keyword = kw_miss,  .cb = serve_miss, .nb_arg_min = 3, .nb_arg_max = 3,
			.nb_types = 2, .types = { CMD_INTEGER, CMD_INTEGER, CMD_INTEGER },	/* seqnum of the read/write, offset, length */
		}
	};
	for (unsigned d=0; d<sizeof_array(def_common); d++) {
		if_fail (mdir_syntax_register(&syntax, def_common+d)) goto q1;
	}
	if (server) for (unsigned d=0; d<sizeof_array(def_server); d++) {
		if_fail (mdir_syntax_register(&syntax, def_server+d)) goto q1;
	} else for (unsigned d=0; d<sizeof_array(def_client); d++) {
		if_fail (mdir_syntax_register(&syntax, def_client+d)) goto q1;
	}
	return;
q1:
	mdir_syntax_dtor(&syntax);
}

/*
 * Low level API
 */

// Boxes

extern inline struct chn_box *chn_box_alloc(size_t bytes);
extern inline void chn_box_free(struct chn_box *box);
extern inline struct chn_box *chn_box_ref(struct chn_box *box);
extern inline void chn_box_unref(struct chn_box *box);
extern inline void *chn_box_unbox(struct chn_box *box);

// TX

static uint_least64_t get_ts(void)
{
	struct timeval tv;
	if (0 != gettimeofday(&tv, NULL)) with_error(errno, "gettimeofday") return 0;
	return (uint_least64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void chn_tx_ctor(struct chn_tx *tx, struct chn_cnx *cnx, bool sender, long long id, struct stream *stream)
{
	tx->cnx = cnx;
	tx->sender = sender;
	tx->status = 0;
	tx->end_offset = 0;
	tx->id = id;
	tx->pth = NULL;
	tx->stream = stream;
	if (stream) stream_ref(stream);
	if_fail (tx->ts = get_ts()) return;
	if (sender) {
		if (stream) {
			if_fail (stream_add_reader(stream, tx)) return;
		}
	}
	TAILQ_INIT(&tx->in_frags);
	TAILQ_INIT(&tx->out_frags);
	LIST_INSERT_HEAD(&cnx->txs, tx, cnx_entry);
}

static void fragment_del(struct fragment *f, struct fragments_queue *q);
void chn_tx_dtor(struct chn_tx *tx)
{
	LIST_REMOVE(tx, cnx_entry);
	struct fragment *f;
	while (NULL != (f = TAILQ_FIRST(&tx->in_frags)))  fragment_del(f, &tx->in_frags);
	while (NULL != (f = TAILQ_FIRST(&tx->out_frags))) fragment_del(f, &tx->out_frags);
	if (tx->pth) {
		pth_cancel(tx->pth);
		tx->pth = NULL;
	}
	if (tx->stream) {
		stream_unref(tx->stream);
		tx->stream = NULL;
	}
}

void chn_tx_del(struct chn_tx *tx)
{
	chn_tx_dtor(tx);
	free(tx);
}

// Fragments (and misses)

// misses are fragment with no ts, no box and no eof flag
struct fragment {
	TAILQ_ENTRY(fragment) tx_entry;
	uint_least64_t ts;	// time of reception/emmission
	struct chn_box *box;	// used only for emmitted fragments
	off_t start, end;
	bool eof;
};

static size_t fragment_size(struct fragment *f) { return f->end - f->start; }

static void out_frag_ctor(struct fragment *f, struct chn_tx *tx, uint_least64_t ts, size_t size, struct chn_box *box, bool eof)
{
	f->start = tx->end_offset;
	tx->end_offset += size;
	f->end = tx->end_offset;
	f->eof = eof;
	f->ts = ts;
	f->box = box ? chn_box_ref(box) : NULL;	// if no box, this is a skip
	TAILQ_INSERT_TAIL(&tx->out_frags, f, tx_entry);
}

static struct fragment *out_frag_new(struct chn_tx *tx, uint_least64_t ts, size_t size, struct chn_box *box, bool eof)
{
	struct fragment *f = malloc(sizeof(*f));
	if (! f) with_error(ENOMEM, "malloc(fragment)") return NULL;
	if_fail (out_frag_ctor(f, tx, ts, size, box, eof)) {
		free(f);
		f = NULL;
	}
	return f;
}

static void insert_by_offset(struct fragments_queue *q, struct fragment *f)
{
	if (TAILQ_EMPTY(q)) {
		TAILQ_INSERT_HEAD(q, f, tx_entry);
		return;
	}
	struct fragment *ff;
	TAILQ_FOREACH(ff, q, tx_entry) {
		if (f->start < ff->start) {
			if (f->end > ff->start) warning("fragments overlap");
			TAILQ_INSERT_BEFORE(ff, f, tx_entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(q, f, tx_entry);
}

static void in_frag_ctor(struct fragment *f, struct chn_tx *tx, uint_least64_t ts, off_t offset, size_t size, bool eof)
{
	f->ts = ts;
	f->start = offset;
	f->end = offset + size;
	f->eof = eof;
	f->box = NULL;
	insert_by_offset(&tx->in_frags, f);
}

static struct fragment *in_frag_new(struct chn_tx *tx, uint_least64_t ts, off_t offset, size_t size, bool eof)
{
	struct fragment *f = malloc(sizeof(*f));
	if (! f) with_error(ENOMEM, "malloc(fragment)") return NULL;
	if_fail (in_frag_ctor(f, tx, ts, offset, size, eof)) {
		free(f);
		f = NULL;
	}
	return f;
}

static void fragment_dtor(struct fragment *f, struct fragments_queue *q)
{
	if (f->box) {
		chn_box_unref(f->box);
		f->box = NULL;
	}
	TAILQ_REMOVE(q, f, tx_entry);
}

static void fragment_del(struct fragment *f, struct fragments_queue *q)
{
	fragment_dtor(f, q);
	free(f);
}

// Commands

struct command {
	char const *keyword;	// create, read or write
	char *resource;	// read or write depending on keyword;
	struct mdir_sent_query sq;
	time_t sent;
	int status;
	struct stream *stream;	// NULL for creat
	pth_cond_t cond;
	pth_mutex_t condmut;
};

static void command_ctor(struct chn_cnx *cnx, struct command *command, char const *kw, char *resource, struct stream *stream, bool rt)
{
	command->keyword = kw;
	command->resource = resource;
	command->sent = time(NULL);
	command->status = 0;
	command->stream = stream;
	if (stream) stream_ref(stream);
	mdir_cnx_query(&cnx->cnx, kw, &command->sq, kw == kw_creat ? (rt ? "*":NULL) : resource, NULL);
	pth_cond_init(&command->cond);
	pth_mutex_init(&command->condmut);
}

static struct command *command_new(struct chn_cnx *cnx, char const *kw, char *resource, struct stream *stream, bool rt)
{
	struct command *command = malloc(sizeof(*command));
	if (! command) with_error(ENOMEM, "malloc(command)") return NULL;
	if_fail (command_ctor(cnx, command, kw, resource, stream, rt)) {
		free(command);
		command = NULL;
	}
	return command;
}

static void command_dtor(struct command *command, struct chn_cnx *cnx)
{
	if (command->status == 0) {
		mdir_cnx_query_cancel(&cnx->cnx, &command->sq);
	}
	if (command->stream) {
		stream_unref(command->stream);
		command->stream = NULL;
	}
}

static void command_del(struct command *command, struct chn_cnx *cnx)
{
	command_dtor(command, cnx);
	free(command);
}

// Sender

void chn_tx_ctor_sender(struct chn_tx *tx, struct chn_cnx *cnx, long long id, struct stream *stream)
{
	chn_tx_ctor(tx, cnx, true, id, stream);
}

static struct chn_tx *chn_tx_new_sender(struct chn_cnx *cnx, long long id, struct stream *stream)
{
	struct chn_tx *tx = malloc(sizeof(*tx));
	if (! tx) with_error(ENOMEM, "malloc(chn_tx)") return NULL;
	if_fail (chn_tx_ctor_sender(tx, cnx, id, stream)) {
		free(tx);
		tx = NULL;
	}
	return tx;
}

static size_t send_chunk(struct chn_tx *tx, struct fragment *f, off_t offset)
{
	size_t sent = f->end - offset;
	if (sent > CHUNK_SIZE) sent = CHUNK_SIZE;
	char params[256];
	(void)snprintf(params, sizeof(params), "%lld %u %zu%s",
		tx->id, (unsigned)offset, fragment_size(f), f->eof ? " *":"");
	if_fail (mdir_cnx_query(&tx->cnx->cnx, f->box ? kw_copy:kw_skip, NULL, params, NULL)) return 0;
	if (f->box) Write(tx->cnx->cnx.fd, f->box->data, sent);
	return sent;
}

static void send_all_chunks(struct chn_tx *tx, struct fragment *f)
{
	off_t offset = f->start, end_offset = f->end;
	do {
		offset += send_chunk(tx, f, offset);
	} while (! is_error() && offset < end_offset);
}

static void retransmit_missed(struct chn_tx *tx)
{
	struct fragment *miss, *f;
	while (NULL != (miss = TAILQ_FIRST(&tx->in_frags))) {
		// warning : a miss from offset X does not imply that all fragments before that
		// were received ; we may miss a miss !
		// look for this chunk
		TAILQ_FOREACH(f, &tx->out_frags, tx_entry) {
			if (miss->end <= f->start || miss->start >= f->end) continue;
			// Send the first chunk from miss->offset
			size_t sent;
			if_fail (sent = send_chunk(tx, f, miss->start)) return;
			if (sent >= fragment_size(miss)) {	// the miss is covered, delete it
				fragment_del(miss, &tx->in_frags);
			} else {
				miss->start += sent;	// Lets go again
			}
			break;
		}
	}
}

static void timeout_fragments(struct fragments_queue *q, uint_least64_t now, uint_least64_t timeout)
{
	struct fragment *f;
	while (NULL != (f = TAILQ_FIRST(q))) {
		if (f->ts + timeout > now) break;
		fragment_del(f, q);
	}
}

void chn_tx_write(struct chn_tx *tx, size_t length, struct chn_box *box, bool eof)
{
	debug("length= %zu, eof= %c", length, eof ? 'y':'n');
	assert(tx->sender == true);
	uint_least64_t now;
	struct fragment *new_frag;
	
	if_fail (now = get_ts()) return;
	if_fail (new_frag = out_frag_new(tx, now, length, box, eof)) return;
	if_fail (retransmit_missed(tx)) return;
	if_fail (send_all_chunks(tx, new_frag)) return;
	if_fail (timeout_fragments(&tx->out_frags, now, OUT_FRAGS_TIMEOUT)) return;

	if (eof) do {	// no more writes : finish everything
		pth_yield(NULL);
		if_fail (retransmit_missed(tx)) return;
		if_fail (now = get_ts()) return;
		if_fail (timeout_fragments(&tx->out_frags, now, OUT_FRAGS_TIMEOUT)) return;
	} while (! chn_tx_status(tx));
}

// Receiver

static void *tx_checker(void *arg);

void chn_tx_ctor_receiver(struct chn_tx *tx, struct chn_cnx *cnx, long long id, struct stream *stream)
{
	if_fail (chn_tx_ctor(tx, cnx, false, id, stream)) return;
	// In addition, a receiving TX much check for missed datas
	tx->pth = pth_spawn(PTH_ATTR_DEFAULT, tx_checker, tx);
}

static struct chn_tx *chn_tx_new_receiver(struct chn_cnx *cnx, long long id, struct stream *stream)
{
	struct chn_tx *tx = malloc(sizeof(*tx));
	if (! tx) with_error(ENOMEM, "malloc(chn_tx)") return NULL;
	if_fail (chn_tx_ctor_receiver(tx, cnx, id, stream)) {
		free(tx);
		tx = NULL;
	}
	return tx;
}

struct chn_box *chn_tx_read(struct chn_tx *tx, off_t *offset, size_t *length, bool *eof)
{
	(void)offset;
	(void)length;
	(void)eof;
	assert(tx->sender == false);
	return NULL;
}

int chn_tx_status(struct chn_tx *tx)
{
	return tx->status;
}

static void finalize_txstart(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	struct mdir_sent_query *sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct chn_cnx *ccnx = DOWNCAST(cnx, cnx, chn_cnx);
	struct command *command = DOWNCAST(sq, sq, command);
	int const status = cmd->args[0].integer;
	if (200 == status) {
		struct chn_tx *tx;
		if (command->keyword == kw_write) {
			tx = chn_tx_new_sender(ccnx, cmd->seq, command->stream);
		} else {
			tx = chn_tx_new_receiver(ccnx, cmd->seq, command->stream);
		}
		error_clear();
	}
	command_del(command, ccnx);
}

static void *reader_thread(void *arg)
{
	debug("New reader thread");
	struct chn_cnx *cnx = arg;
	do {
		pth_yield(NULL);
		/* The channel reader thread have callbacks that will receive incomming
		 * datas and write it to the stream it's associated to (ie will perform
		 * physical writes onto other TXs and/or file :
		 *
		 * - The service for the read command (download) will just lookup/create
		 * the corresponding stream, and associate it's output to the TX. It does
		 * not answer the query.
		 * 
		 * - The service for the write command (upload) will do the same, but
		 * associate this TX to this stream's input. Doesn't answer neither.
		 *
		 * - The services for copy/skip will send the data to the stream, up to
		 * their associated TXs/file.
		 *
		 * - The service for miss is handled by the chn_retransmit() facility.
		 *
		 * - The finalizer for sent commands creates the TX, or return the created
		 * resource to the user.
		 *
		 * When a stream is created for a file, a new thread is created that will
		 * read it and write data to all it's associated reading TX, untill there
		 * is no more and the thread is killed and the stream closed.  This is
		 * not the stream but the sending TX that are responsible for keeping
		 * copies (actualy just references) of the sent data boxes for
		 * retransmissions, because several TX may have different needs in this
		 * respect (different QoS, different locations on the file, etc...). And
		 * it's simplier.
		 */
		if_fail (mdir_cnx_read(&cnx->cnx)) break;
	} while (! cnx->quit);	// FIXME: or if the reader quits for some reason
	error_clear();
	chn_cnx_del(cnx);
	return NULL;
}

static void chn_cnx_ctor(struct chn_cnx *cnx)
{
	cnx->quit = false;
	LIST_INIT(&cnx->txs);
	cnx->reader = pth_spawn(PTH_ATTR_DEFAULT, reader_thread, cnx);
	if (! cnx->reader) with_error(0, "pth_spawn(chn_cnx reader)") return;
}

void chn_cnx_ctor_outbound(struct chn_cnx *cnx, char const *host, char const *service, char const *username)
{
	if_fail (mdir_cnx_ctor_outbound(&cnx->cnx, &syntax, host, service, username)) return;
	chn_cnx_ctor(cnx);
}

struct chn_cnx *chn_cnx_new_outbound(char const *host, char const *service, char const *username)
{
	struct chn_cnx *cnx = malloc(sizeof(*cnx));
	if (! cnx) with_error(ENOMEM, "malloc(chn_cnx)") return NULL;
	if_fail (chn_cnx_ctor_outbound(cnx, host, service, username)) {
		free(cnx);
		cnx = NULL;
	}
	return cnx;
}

void chn_cnx_ctor_inbound(struct chn_cnx *cnx, int fd)
{
	if_fail (mdir_cnx_ctor_inbound(&cnx->cnx, &syntax, fd)) return;
	chn_cnx_ctor(cnx);
}

struct chn_cnx *chn_cnx_new_inbound(int fd)
{
	struct chn_cnx *cnx = malloc(sizeof(*cnx));
	if (! cnx) with_error(ENOMEM, "malloc(chn_cnx)") return NULL;
	if_fail (chn_cnx_ctor_inbound(cnx, fd)) {
		free(cnx);
		cnx = NULL;
	}
	return cnx;
}

void chn_cnx_dtor(struct chn_cnx *cnx)
{
	if (cnx->reader) {
		(void)pth_cancel(cnx->reader);
		cnx->reader = NULL;
	}
	struct chn_tx *tx;
	while (NULL != (tx = LIST_FIRST(&cnx->txs))) {
		chn_tx_del(tx);
	}
	mdir_cnx_dtor(&cnx->cnx);
}

void chn_cnx_del(struct chn_cnx *cnx)
{
	chn_cnx_dtor(cnx);
	free(cnx);
}

/*
 * Get a file from cache (download it first if necessary)
 */
#if 0
static void fetch_file_with_cnx(struct chn_cnx *cnx, char const *name, int fd)
{
	struct txstart_param param;
	if_fail (chn_tx_ctor(&param.tx, cnx, false, name, 0)) return;
	if_fail (mdir_cnx_query(&cnx->cnx, kw_read, &param.sq, name, NULL)) {
		error_save();
		chn_tx_dtor(&param.tx);
		error_restore();
		return;
	}
	bool eof_received = false;
	do {
		struct chn_box *box;
		off_t offset;
		size_t length;
		bool eof;
		if_fail (box = chn_tx_read(&tx, &offset, &length, &eof)) break;
		WriteTo(fd, offset, box->data, length);
		chn_box_unref(box);
		on_error break;
		if (eof) eof_received = true;
	} while (! chn_tx_status(&tx));
	if (200 != chn_tx_status(&tx)) error_push(0, "Transfert incomplete");
	chn_tx_dtor(&tx);
}

int chn_get_file(struct chn_cnx *cnx, char *localfile, size_t len, char const *name)
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
	fetch_file_with_cnx(cnx, name, fd);
	on_error {
		// delete the file if the transfert failed for some reason
		if (0 != unlink(localfile)) error_push(errno, "Failed to download %s, but cannot unlink it", localfile);
	}
	return actual_len;
}
#endif

/*
 * Request a new file name
 */

static void finalize_creat(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	assert(cmd->def->keyword == kw_creat);
	struct mdir_sent_query *sq = mdir_cnx_query_retrieve(cnx, cmd);
	on_error return;
	struct command *command = DOWNCAST(sq, sq, command);
	if (200 == cmd->args[0].integer) {
		snprintf(command->resource, PATH_MAX, "%s", cmd->args[1].string);
	}
	(void)pth_cond_notify(&command->cond, TRUE);
}

void chn_create(struct chn_cnx *cnx, char *name, bool rt)
{
	assert(! server);
	struct command *command;
	if_fail (command = command_new(cnx, kw_creat, name, NULL, rt)) return;
	name[0] = '\0';
	(void)pth_mutex_acquire(&command->condmut, FALSE, NULL);
	(void)pth_cond_await(&command->cond, &command->condmut, NULL);
	(void)pth_mutex_release(&command->condmut);
	if (name[0] == '\0') with_error(0, "No answer to create request") return;
}

/*
 * Send a local file to a channel
 */
#if 0
void chn_send_file(struct chn_cnx *cnx, char const *name, int fd)
{
	off_t offset = 0, max_offset;
	if_fail (max_offset = filesize(fd)) return;
	struct chn_tx tx;
	if_fail (chn_tx_ctor(&tx, cnx, true, name, 0)) return;
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
		unless_error chn_tx_write(&tx, len, box, eof);
		offset += len;
		on_error break;
	} while (offset < max_offset);
#	undef READ_FILE_BLOCK
	chn_tx_dtor(&tx);
}
#endif
/*
 * Transfert commands handlers
 */

static struct chn_tx *find_rtx(struct chn_cnx *cnx, long long id)
{
	struct chn_tx *tx;
	LIST_FOREACH(tx, &cnx->txs, cnx_entry) {
		if (tx->sender) continue;	// we are looking for a receiver
		if (tx->id == id) return tx;
	}
	return NULL;
}

static void ask_retransmit(struct chn_tx *tx, off_t offset, size_t size)
{
	char cmd[256];
	int len = snprintf(cmd, sizeof(cmd), "%s %llu %u %zu\n", kw_miss, tx->id, (unsigned)offset, size);
	Write(tx->cnx->cnx.fd, cmd, len);
}

static bool was_long_ago(uint_least64_t ts, uint_least64_t now)
{
	return ts + RETRANSM_TIMEOUT < now;
}

static void check_in_frags(struct chn_tx *tx, uint_least64_t now)
{
	struct fragment *f, *tmp, *last_frag = NULL;
	off_t last_end = 0;
	TAILQ_FOREACH_SAFE(f, &tx->in_frags, tx_entry, tmp) {
		if (f->start > last_end && was_long_ago(f->ts, now)) {
			if_fail (ask_retransmit(tx, last_end, f->start - last_end)) return;
			f->ts = now;
		} else if (last_frag) {	// merge those two fragments
			if (f->end > last_frag->end) last_frag->end = f->end;
			fragment_del(f, &tx->in_frags);
			last_end = last_frag->end;
			continue;
		}
		last_end = f->end;
		last_frag = f;
	}
	if (last_frag) {
		if (! last_frag->eof && was_long_ago(last_frag->ts, now)) {
			if_fail (ask_retransmit(tx, last_frag->end, 1)) return;
			last_frag->ts = now;
		}
	} else {	// not received anything yet
		if (was_long_ago(tx->ts, now)) {
			if_fail (ask_retransmit(tx, 0, 1)) return;
			tx->ts = now;
		}
	}
}

static void serve_copy(struct mdir_cmd *cmd, void *cnx_)
{
	struct chn_cnx *cnx = DOWNCAST(cnx_, cnx, chn_cnx);
	long long id  = cmd->args[0].integer;
	off_t offset  = cmd->args[1].integer;
	size_t size = cmd->args[2].integer;
	bool eof = cmd->nb_args == 4;
	debug("id=%lld, offset=%u, size=%zu, eof=%s", id, (unsigned)offset, size, eof ? "y":"n");
	struct chn_tx *tx = find_rtx(cnx, id);
	if (! tx) {
		mdir_cnx_answer(&cnx->cnx, cmd, 500, "No such tx id");
		return;
	}
	struct chn_box *box = chn_box_alloc(size);
	if (! box) {
		mdir_cnx_answer(&cnx->cnx, cmd, 501, "Cannot malloc(data)");
		return;
	}
	// Read available datas from fd
	if_fail (Read(box->data, cnx->cnx.fd, size)) {
		error_clear();
		chn_box_free(box);
		mdir_cnx_answer(&cnx->cnx, cmd, 501, "Cannot read data");
		return;
	}
	// Give it to our callback (filed will propagates it onto streams, client will write it to a file or do whatever he wants with this).
	if_fail (stream_write(tx->stream, offset, size, box, eof)) {
		mdir_cnx_answer(&cnx->cnx, cmd, 501, error_str());
		error_clear();
		tx->status = 501;
		chn_box_unref(box);
		return;
	}
	chn_box_unref(box);
	box = NULL;
	mdir_cnx_answer(&cnx->cnx, cmd, 200, "OK");
	// Register that we received this fragment.
	uint_least64_t ts;
	if_fail (ts = get_ts()) return;
	if_fail ((void)in_frag_new(tx, ts, offset, size, eof)) return;
	if_fail (check_in_frags(tx, ts)) return;
	// Check weither the TX is over
	struct fragment *first, *last;
	first = TAILQ_FIRST(&tx->in_frags);
	last = TAILQ_LAST(&tx->in_frags, fragments_queue);
	if (first == last && last->eof && first->start == 0) {
		tx->status = 200;
		// Shall we destroy this TX then ?
	}
}

static void serve_skip(struct mdir_cmd *cmd, void *cnx_)
{
	struct chn_cnx *cnx = DOWNCAST(cnx_, cnx, chn_cnx);
	long long id  = cmd->args[0].integer;
	off_t offset  = cmd->args[1].integer;
	size_t size = cmd->args[2].integer;
	bool eof = cmd->nb_args == 4;
	// TODO
	(void)cnx;
	(void)offset;
	(void)size;
	(void)eof;
	(void)id;
}

static void serve_miss(struct mdir_cmd *cmd, void *cnx_)
{
	struct chn_cnx *cnx = DOWNCAST(cnx_, cnx, chn_cnx);
	long long id  = cmd->args[0].integer;
	off_t offset  = cmd->args[1].integer;
	size_t size = cmd->args[2].integer;
	// TODO
	(void)cnx;
	(void)offset;
	(void)size;
	(void)id;
}

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
		int len = snprintf(path, sizeof(path), "%s/XXXXXX", chn_files_root);
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

static void serve_read_write(struct mdir_cmd *cmd, void *user_data, bool reader)
{
	struct mdir_cnx *cnx = user_data;
	struct chn_cnx *ccnx = DOWNCAST(cnx, cnx, chn_cnx);
	char const *name = cmd->args[0].string;
	struct stream *stream;
	struct chn_tx *tx;
	
	if (cmd->seq == -1) {
		mdir_cnx_answer(cnx, cmd, 500, "Missing seqnum");
		return;
	}
	if_fail (stream = stream_lookup(name)) {
		error_clear();	// FIXME: error_str() cant work after error_clear()
		mdir_cnx_answer(cnx, cmd, 500, error_str());
		return;
	}
	if (reader) {
		tx = chn_tx_new_receiver(ccnx, cmd->seq, stream);
	} else {
		tx = chn_tx_new_sender(ccnx, cmd->seq, stream);
	}
	on_error {
		error_clear();
		stream_unref(stream);
		mdir_cnx_answer(cnx, cmd, 500, error_str());
		return;
	}
	mdir_cnx_answer(cnx, cmd, 200, "Ok");
}

static void serve_write(struct mdir_cmd *cmd, void *user_data)
{
	serve_read_write(cmd, user_data, false);
}

static void serve_read(struct mdir_cmd *cmd, void *user_data)
{
	serve_read_write(cmd, user_data, true);
}

static void serve_quit(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *cnx = user_data;
	struct chn_cnx *ccnx = DOWNCAST(cnx, cnx, chn_cnx);
	ccnx->quit = true;
	mdir_cnx_answer(cnx, cmd, 200, "Ok");
}

static void *tx_checker(void *arg)
{
	struct chn_tx *tx = arg;
	uint_least64_t now;
	while (1) {
		if_fail (now = get_ts()) return NULL;
		if_fail (check_in_frags(tx, now)) return NULL;
		pth_usleep(RETRANSM_TIMEOUT);
	}
	return NULL;
}

