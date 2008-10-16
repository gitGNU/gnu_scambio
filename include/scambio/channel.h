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
 * required to auth.
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

#if 0
static char const *const kw_creat = "auth";	// create a persistent file
static char const *const kw_creat = "creat";	// create a persistent file
static char const *const kw_chan  = "chan";	// create a RT channel
static char const *const kw_write = "write";	// write onto a file/channel (all are forwarded to readers, file are also writtento in the file store)
static char const *const kw_read  = "read";	// read a file/channel
static char const *const kw_copy  = "copy";	// transfert datas (parameters are offset, length, EOF flag)
static char const *const kw_skip  = "skip";	// same as copy, but without the datas
static char const *const kw_miss  = "miss";	// report a gap in the transfered stream

	cmd_register_keyword(&parser, kw_creat, 0, 0, CMD_EOA);
	cmd_register_keyword(&parser, kw_chan,  0, 0, CMD_EOA);
	cmd_register_keyword(&parser, kw_write, 1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(&parser, kw_read,  1, 1, CMD_STRING, CMD_EOA);
	cmd_register_keyword(&parser, kw_copy,  2, 3, CMD_INTEGER, CMD_INTEGER, CMD_STRING, CMD_EOA);
	cmd_register_keyword(&parser, kw_skip,  2, 2, CMD_INTEGER, CMD_INTEGER, CMD_EOA);
	cmd_register_keyword(&parser, kw_miss,  2, 2, CMD_INTEGER, CMD_INTEGER, CMD_EOA);
#endif

/* High level API (for clients only) */

/* Will return the name of a file containing the full content.
 * (taken from the file cache).
 * If the cache lacks the file, it's downloaded first.
 * TODO: The file might be removed by the cache cleaner after this call
 * and before a subsequent open. Its touched to lower the risks.
 * Returns the actual length of the filename.
 */
int chn_get_file(char *filename, size_t len, char const *name, char const *username);

/* Request a new channel, optionnaly for realtime.
 * Will connect if not already.
 * Note : realtime channels are handled the same by both sides, except that
 * a file is kept if the channel is not realtime.
 * Thus it is not advisable to chn_get_file() a RT channel.
 */
void chn_create(char *name, size_t len, bool rt, char const *username);

/* Write a local file to a channel
 */
void chn_send_file(char const *name, int fd, char const *username);

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
static inline void chn_box_ref(struct chn_box *box) { box->count ++; }
static inline void chn_box_unref(struct chn_box *box) { if (--box->count <= 0) chn_box_free(box); }
static inline void *chn_box_unbox(struct chn_box *box) { return box->data; }

/* Struct chn_wtx describes the state of an outgoing transfert, the associated threads,
 * the retransmissions, etc... while struct chn_rtx describes the state of an incomming
 * transfert.
 */
struct chn_wtx;
struct chn_rtx;

/* Send the write command, if the cnx is OK (and authed), and start the reader thread.
 * Notes :
 * - the writer thread is the caller, in order to wait for data to be sent before fetching new ones.
 * - the reader thread will flag the missed segments in the chn_wtx, and the next write will
 *   start by retransmitting them before sending the new data. Thus retransmissions will not be sent
 *   if the writer stops writing. This is not a problem in practice since the writer will just
 *   read new data / write those data in a tight loop. The only problem is if we are bloked in the
 *   read or the write. for the write, its not a problem since UDP wont do that and any way we could
 *   not retransmitter neither then. For the read, well, dont do blocking reads :)
 */
struct chn_wtx *chn_wtx_new(struct mdir_cnx *, char const *name);

/* Send the data according to the MTU (ie, given block may be split again).
 * This first sent the required retransmissions, then free the old box, then send the given box by packet
 * of MTU bytes. The last packet receive the eof flag.
 * If the eof flag is set, there will be no more writes. Waits for the server answer (ie wait for
 * the reader thread to exit), while retransmitting segments and freeing boxes as required, and
 * yielding CPU to the reader.
 */
void chn_wtx_write(struct chn_wtx *tx, off_t offset, size_t length, struct chn_box *box, bool eof);

/* Once you are done with the transfert, free it
 */
void chn_wtx_del(struct chn_wtx *tx);

/* Send the read command (check that the cnx is authentified), and init a new chn_rtx
 * with a new writer thread that will ask for retransmissions of missed packets.
 * The reader thread is now the caller.
 */
struct chn_rtx *chn_rtx_new(struct mdir_cnx *, char const *name);

/* Return the next data block received (not necessarily in sequence).
 * This is allocated in a box so that you can rewrite it to another channel if you wish,
 * but when returned you own the only ref to it.
 * Note : eof means that the block received is at the end of the datas, NOT that it will
 * be the last received.
 */
struct chn_box *chn_rtx_read(struct chn_rtx *tx, off_t *offset, size_t *length, bool *eof);

/* Since you do not read the blocks in order, eof does not indicate that you can stop reading
 * and trash your chn_rtx. This one tells you if you read all data from start to EOF.
 */
bool chn_rtx_complete(struct chn_rtx *tx);

/* But if the other peer changed its mind, you may never receive it.
 * This one tell you if there are still hope to receive the missing data.
 */
bool chn_rtx_should_wait(struct chn_rtx *tx);

/* Once you are done with the transfert, free it
 */
void chn_rtx_del(struct chn_rtx *tx);

#endif
