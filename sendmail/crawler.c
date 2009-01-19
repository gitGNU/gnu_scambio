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
#include <assert.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/header.h"
#include "scambio/timetools.h"
#include "sendmail.h"

/*
 * Data Definitions
 */

static struct mdir *to_send, *sent;
static struct mdir_cursor to_send_cursor = MDIR_CURSOR_INITIALIZER;
static pth_t crawler_id, acker_id; 

/*
 * Crawler thread
 * Reads all patches in to_send and build forward from them.
 */

static void send_patch(struct mdir *mdir, struct header *header, mdir_version version, void *data)
{
	(void)data;
	assert(mdir == to_send);
	if (version < 0) return;	// we do not want to try to send anything if it's not synched
	if (! header_has_type(header, SC_MAIL_TYPE)) return;
	if (! header_find(header, SC_FROM_FIELD, NULL)) {
		debug("No From, skip");
		return;
	}
	if (! header_find(header, SC_DESCR_FIELD, NULL)) {
		debug("No subject, skip");
		return;
	}
	if (! header_find(header, SC_TO_FIELD, NULL)) {
		debug("No dests, skip");
		return;
	}
	struct forward *fwd = forward_new(version, header);
	unless_error forward_submit(fwd);
}

static void *crawler_thread(void *data)
{
	(void)data;
	while (! is_error() && ! terminate) {
		mdir_patch_list(to_send, &to_send_cursor, false, send_patch, NULL, NULL, NULL);
		(void)pth_sleep(5);	// FIXME: a signal when something new is received ?
	}
	debug("Exiting crawler thread");
	return NULL;
}

/*
 * Acker thread
 * Read delivered forwards, and move the patches from to_send to sent with appropriate status.
 */

static struct mdir *get_or_create_mdir(char const *sender)
{
	debug("for sender = '%s'", sender);
	char sentbox[PATH_MAX];
	int len = snprintf(sentbox, sizeof(sentbox), "%s/", mdir_id(sent));
	char const *subdir_name = sentbox + len;
	for (char const *c = sender; len < (int)sizeof(sentbox)-1 && *c; c++, len++) {
		sentbox[len] = *c != '/' ? *c : '_';
	}
	sentbox[len] = '\0';
	struct mdir *mdir = mdir_lookup(sentbox);
	unless_error return mdir;
	error_clear();
	// Creates the mdir first
	struct header *h = header_new();
	(void)header_field_new(h, SC_TYPE_FIELD, SC_DIR_TYPE);
	(void)header_field_new(h, SC_NAME_FIELD, subdir_name);
	if_fail (mdir_patch_request(sent, MDIR_ADD, h)) return NULL;
	header_unref(h);
	// And give him to the sender
	if_fail (mdir = mdir_lookup(sentbox)) return NULL;
	h = header_new();
	(void)header_field_new(h, SC_TYPE_FIELD, SC_PERM_TYPE);
	(void)header_field_new(h, SC_ALLOW_ADMIN_FIELD, sender);
	(void)header_field_new(h, SC_ALLOW_WRITE_FIELD, conf_get_str("SC_USERNAME"));
	(void)header_field_new(h, SC_DENY_READ_FIELD, "*");
	if_fail (mdir_patch_request(mdir, MDIR_ADD, h)) return NULL;
	header_unref(h);
	// Then return the mdir
	return mdir;
}

static void move_fwd(struct forward *fwd)
{
	debug("version=%"PRIversion, fwd->version);
	// Retrieve the patch (should we keep it in forward with ref counter ?)
	struct header *header = mdir_read(to_send, fwd->version, NULL);
	on_error return;
	assert(header);
	// We then delete this message so that it will never be sent again
	if_fail (mdir_del_request(to_send, fwd->version)) return;
	// And copy it (modified) to the sent mdir
	(void)header_field_new(header, SC_STOP_FIELD, sc_ts2gmfield(time(NULL), true));
	char status[8];
	snprintf(status, sizeof(status), "%d", fwd->status);
	(void)header_field_new(header, SC_STATUS_FIELD, status);
	// Write it to the user's sentbox
	struct header_field *from_field = header_find(header, SC_FROM_FIELD, NULL);	// instead of the from field, use a field identifying scambio user, so we can delegate him the admin of this dir if we create it
	if (from_field) {
		struct mdir *sentbox = get_or_create_mdir(from_field->value);
		unless_error mdir_patch_request(sentbox, MDIR_ADD, header);
	}
	header_unref(header);
}

static void *acker_thread(void *data)
{
	(void)data;
	struct forward *fwd;
	while (! is_error() && ! terminate) {
		while (NULL != (fwd = forward_oldest_completed())) {
			move_fwd(fwd);
			forward_del(fwd);
		}
		(void)pth_sleep(1);	// FIXME: a signal from forwarder to acker when the list becomes non empty
	}
	debug("Exiting acker thread");
	return NULL;
}

/*
 * Init
 */

void crawler_begin(void)
{
	conf_set_default_str("SC_SMTP_TO_SEND", "mailboxes/To_Send");
	conf_set_default_str("SC_SMTP_SENT", "mailboxes/Sent");
	if_fail (to_send = mdir_lookup(conf_get_str("SC_SMTP_TO_SEND"))) return;
	if_fail (sent = mdir_lookup(conf_get_str("SC_SMTP_SENT"))) return;
	// start thread
	crawler_id = pth_spawn(PTH_ATTR_DEFAULT, crawler_thread, NULL);
	if (NULL == crawler_id) with_error(0, "Cannot spawn crawler") return;
	acker_id = pth_spawn(PTH_ATTR_DEFAULT, acker_thread, NULL);
	if (NULL == acker_id) {
		(void)pth_abort(crawler_id);
		with_error(0, "Cannot spawn acker") return;
	}
}

void crawler_end(void)
{
	if (crawler_id) {
		(void)pth_abort(crawler_id);
		crawler_id = NULL;
	}
	if (acker_id) {
		(void)pth_abort(acker_id);
		acker_id = NULL;
	}
}
