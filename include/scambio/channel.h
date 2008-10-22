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
/*
 * Channels
 *
 * This is made a distinct protocol that the mdir protocol because this one
 * is reciprocal : any peer can send or receive data on a channel.
 * The only exception is that only the client can create a channel and is
 * required to auth. Also, the client asks for the transfert (write/read command).
 * Then, the transfert itself (copy, skip, miss) is reciprocal.
 * So the same parser can be used on the client (a plugin) than on the channel
 * server.
 */
#ifndef CHANNEL_H_081015
#define CHANNEL_H_081015

#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <scambio/cnx.h>

/*
 * Init
 *
 * Uses MDIR_FILES_DIR to locate file cache
 */

void chn_begin(void);
void chn_end(void);

/* High level API (for clients only) */

/* A chn_cnx is merely a mdir_cnx with a list of handled chn_txs, and a dedicated reading thread.
 */
struct chn_cnx {
	struct mdir_cnx cnx;
	pth_t reader;
	LIST_HEAD(chn_txs, chn_tx) txs;
};

void chn_cnx_ctor_outbound(struct chn_cnx *cnx, char const *host, char const *service, char const *username);
void chn_cnx_ctor_inbound(struct chn_cnx *cnx, struct mdir_syntax *syntax, int fd);
void chn_cnx_dtor(struct chn_cnx *cnx);

/* Will return the name of a file containing the full content.
 * (taken from the file cache).
 * If the cache lacks the file, it's downloaded first.
 * TODO: The file might be removed by the cache cleaner after this call
 * and before a subsequent open. Its touched to lower the risks.
 * Returns the actual length of the filename.
 */
int chn_get_file(struct chn_cnx *cnx, char *localfile, size_t len, char const *name);

/* Request a new channel, optionnaly for realtime.
 * Will wait untill creation or timeout.
 * Note : realtime channels are handled the same by both sides, except that
 * a file is kept if the channel is not realtime.
 * Thus it is not advisable to chn_get_file() a RT channel.
 */
void chn_create(struct chn_cnx *cnx, char *name, size_t len, bool rt);

/* Write a local file to a channel
 */
void chn_send_file(struct chn_cnx *cnx, char const *name, int fd);

/* Low level API */

/* Since there are retransmissions data may be required to be send more than once. But we do not want
 * to have the whole file in memory, and we do not want the user of this API to handle retransmissions.
 * So we provide a special ref counted data container : chn_box
 */
struct chn_box {
	int count;
	char data[];
};

static inline struct chn_box *chn_box_alloc(size_t bytes)
{
	struct chn_box *box = malloc(bytes + sizeof(*box));
	if (box) box->count = 1;
	return box;
}
static inline void chn_box_free(struct chn_box *box) { free(box); }
static inline struct chn_box *chn_box_ref(struct chn_box *box) { box->count ++; return box; }
static inline void chn_box_unref(struct chn_box *box) { if (--box->count <= 0) chn_box_free(box); }
static inline void *chn_box_unbox(struct chn_box *box) { return box->data; }

/* Struct chn_tx describe the state of an ongoing transfert.
 * the main thread, aka writer thread (the one that calls these functions and that writes on
 * the socket) handle the writes to the writing txs and the reads from the reading txs,
 * while a dedicated reader thread loop over mdir_cnx_read(), handling all of the data
 * transfert commands.
 * Callbacks retrieve the corresponding chn_tx by the id present in the command.
 * Notes for the sending peer :
 * - the writer thread is the caller, in order to wait for data to be sent before fetching new ones.
 * - the reader thread will flag the missed segments in the chn_tx, and the next write will
 *   start by retransmitting them before sending the new data. Thus retransmissions will not be sent
 *   if the writer stops writing. This is not a problem in practice since the writer will just
 *   read new data / write those data in a tight loop. The only problem is if we are bloked in the
 *   read or the write. for the write, its not a problem since UDP won't do that and any way we could
 *   not retransmitter neither then. For the read, well, don't do blocking reads :)
 * Notes for the receiving peer :
 *
 */
struct chn_tx;

/* For clients, start by sending the write command. Then start the reader thread (ie. add
 * the chn_tx to the pool of tx known of the reader thread).
 */
struct chn_tx *chn_tx_new_sender(struct chn_cnx *, char const *name, long long id);

/* Send the read command (check that the cnx is authentified), and init a new chn_tx.
 */
struct chn_tx *chn_tx_new_receiver(struct chn_cnx *, char const *name, long long id);

/* Send the data according to the MTU (ie, given block may be split again).
 * This first sent the required retransmissions, then send the given box by packet of
 * MTU bytes, then free the old box. The last packet receive the eof flag.
 * If the eof flag is set, there will be no more writes. Waits for the server answer (ie wait for
 * the reader thread to exit), while retransmitting segments and freeing boxes as required, and
 * yielding CPU to the reader.
 * Set box to NULL to skip data.
 */
void chn_tx_write(struct chn_tx *tx, size_t length, struct chn_box *box, bool eof);

/* Return the next data block received (not necessarily in sequence).
 * This is allocated in a box so that you can rewrite it to another channel if you wish,
 * but when returned you own the only ref to it.
 * Note : eof means that the block received is at the end of the datas, NOT that it will
 * be the last received.
 */
struct chn_box *chn_tx_read(struct chn_tx *tx, off_t *offset, size_t *length, bool *eof);

/* Since you do not read the blocks in order, eof does not indicate that you can stop reading
 * and trash your chn_rtx. This one tells you if you read all data available.
 * Returns the status (0 meaning not terminated, 200 meaning done, anything else being an error).
 */
int chn_tx_status(struct chn_tx *tx);

/* Once you are done with the transfert, free it
 */
void chn_tx_del(struct chn_tx *tx);

#endif
