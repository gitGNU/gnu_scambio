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
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/header.h"
#include "misc.h"
#include "varbuf.h"
#include "sendmail.h"

/*
 * Data Definitions
 */

static pth_mutex_t list_mutex;	// protect the following lists (and the entry field of forwards when they are in)
// Oldest forwards first
static struct forwards waiting_forwards;
static struct forwards delivered_forwards;	// not necessarily successfully of course

#define CNX_IDLE_TIMEOUT 15	// close useless SMTP cnx after this delay in secs.
static int sock_fd;
static time_t sock_last_used;
static char my_hostname[256];

static pth_t forwarder_thread;

/*
 * List manipulation
 */

static void list_lock(void)
{
	pth_mutex_acquire(&list_mutex, FALSE, NULL);
}

static void list_unlock(void)
{
	pth_mutex_release(&list_mutex);
}

static void list_empty(struct forwards *list)
{
	list_lock();
	struct forward *fwd;
	while (NULL != (fwd = TAILQ_FIRST(list))) {
		assert(fwd->list == list);
		forward_del(fwd);
	}
	list_unlock();
}

static void list_move_to(struct forwards *list, struct forward *fwd)
{
	// Lock is not necessary here because the thread scheduler is not called here.
	list_lock();
	if (fwd->list) {
		TAILQ_REMOVE(fwd->list, fwd, entry);
	}
	TAILQ_INSERT_TAIL(list, fwd, entry);
	fwd->list = list;
	list_unlock();
}

/*
 * Dealing with connection to SMTP relay
 */

/* The maximum total length of a command line including the command
 * word and the <CRLF> is 512 characters.  SMTP extensions may be
 * used to increase this limit. -- RFC 2821
 */
#define MAX_SMTP_LINE 512
#define CRLF "\r\n"

static void send_smtp_nstrs(int fd, va_list ap)
{
	char cmd[MAX_SMTP_LINE];
	unsigned len = 0;
	char const *arg;
	while (len < sizeof(cmd) && NULL != (arg = va_arg(ap, char const *))) {
		len += snprintf(cmd+len, sizeof(cmd)-len, arg);
	}
	if (len < sizeof(cmd)) len += snprintf(cmd+len, sizeof(cmd)-len, CRLF);
	if (len >= sizeof(cmd)) with_error(0, "Command too long : '%s...'", cmd) return;
	Write(fd, cmd, len);
}

static void
#ifdef __GNUC__
	__attribute__ ((sentinel))
#endif
send_smtp_strs(int fd, ...)
{
	va_list ap;
	va_start(ap, fd);
	send_smtp_nstrs(fd, ap);
	va_end(ap);
}

static int read_smtp_status(int fd, struct varbuf *vb)
{
	int status = -1;
	varbuf_clean(vb);
	char *new, *end;
	do {
		if_fail (varbuf_read_line(vb, fd, MAX_SMTP_LINE*2 /* don't be pedantic */, &new)) return -1;
		int s = strtoul(new, &end, 10);
		if (end != new+3) warning("Suspicious SMTP status (%d)", s);
		if (status != -1 && s != status) warning("Status change in multiline response, from %d to %d", status, s);
		status = s;
	} while (*end == '-');
	if (*end != ' ') with_error(0, "Cannot get status from SMTP answer") return 0;
	return status;
}

static int
#ifdef __GNUC__
	__attribute__ ((sentinel))
#endif
smtp_cmd(int fd, ...)
{
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, MAX_SMTP_LINE*5, true)) return 0;	// make room for a multiline answer
	int status = 0;
	va_list ap;
	va_start(ap, fd);
	if_succeed (send_smtp_nstrs(fd, ap)) {
		status = read_smtp_status(fd, &vb);
	}
	va_end(ap);
	error_save();
	varbuf_dtor(&vb);
	error_restore();
	return status;
}

static void may_open_connection(void)
{
	if (sock_fd != -1) return;
	if_fail (sock_fd = Connect(conf_get_str("SC_SMTP_RELAY_HOST"), conf_get_str("SC_SMTP_RELAY_PORT"))) return;
	// Welcome message
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, MAX_SMTP_LINE, true)) return;
	int status = read_smtp_status(sock_fd, &vb);
	varbuf_dtor(&vb);
	if (is_error() || status != 220) {
		(void)close(sock_fd);
		sock_fd = -1;
		with_error(0, "Welcome message is not welcoming (status %d)", status) return;
	}
	// HELO
	status = smtp_cmd(sock_fd, "HELO ", my_hostname, NULL);
	if (is_error() || status != 250) {
		(void)close(sock_fd);
		sock_fd = -1;
		with_error(0, "Cannot HELO to SMTP relay : status %d", status) return;
	}
	sock_last_used = time(NULL);
}

static void may_close_connection(void)
{
	if (sock_fd == -1) return;
	unsigned delay = time(NULL) - sock_last_used;
	if (delay > CNX_IDLE_TIMEOUT) {
		debug("Closing idle SMTP cnx");
		(void)smtp_cmd(sock_fd, "QUIT", NULL);
		error_clear();
		(void)close(sock_fd);
		sock_fd = -1;
	}
}

static void send_file(int out, char const *filename)
{
	static const unsigned char b64[64] = {
		[0] = 'A',  [1] = 'B',  [2] = 'C',  [3] = 'D',  [4] = 'E',  [5] = 'F', 
		[6] = 'G',  [7] = 'H',  [8] = 'I',  [9] = 'J',  [10] = 'K', [11] = 'L',
		[12] = 'M', [13] = 'N', [14] = 'O', [15] = 'P', [16] = 'Q', [17] = 'R',
		[18] = 'S', [19] = 'T', [20] = 'U', [21] = 'V', [22] = 'W', [23] = 'X',
		[24] = 'Y', [25] = 'Z', [26] = 'a', [27] = 'b', [28] = 'c', [29] = 'd',
		[30] = 'e', [31] = 'f', [32] = 'g', [33] = 'h', [34] = 'i', [35] = 'j',
		[36] = 'k', [37] = 'l', [38] = 'm', [39] = 'n', [40] = 'o', [41] = 'p',
		[42] = 'q', [43] = 'r', [44] = 's', [45] = 't', [46] = 'u', [47] = 'v',
		[48] = 'w', [49] = 'x', [50] = 'y', [51] = 'z', [52] = '0', [53] = '1',
		[54] = '2', [55] = '3', [56] = '4', [57] = '5', [58] = '6', [59] = '7',
		[60] = '8', [61] = '9', [62] = '+', [63] = '/',
  	};
	ssize_t len;
	unsigned width = 0;
	int in = open(filename, O_RDONLY);
	if (in < 0) with_error(errno, "open(%s)", filename) return;
	do {
		unsigned char inbuf[3];
		memset(inbuf, 0, sizeof(inbuf));
		len = pth_read(in, inbuf, sizeof(inbuf));
		if (len < 0) with_error(errno, "read(%s)", filename) break;
		if (len == 0) break;
		unsigned char outbuf[4] = {
			b64[inbuf[0] >> 2],
			b64[((inbuf[0] & 0x3) << 4) | (inbuf[1] >> 4)],
			len > 1 ? b64[((inbuf[1] & 0xF) << 2) | (inbuf[2] >> 6)] : '=',
			len == 3 ? b64[inbuf[2] & 0x3F] : '=',
		};
		width += 4;
		if (width >= 76) {
			if_fail (Write(out, CRLF, 2)) break;
			width = 0;
		}
		if_fail (Write(out, outbuf, sizeof(outbuf))) break;
	} while (len == 3);
	(void)close(in);
}

static void send_part(int fd, struct part *part)
{
	if_fail (send_smtp_strs(fd, "Content-Transfer-Encoding: base64", NULL)) return;
	if (part->type[0] != '\0') {
		if_fail (send_smtp_strs(fd, "Content-Type: ", part->type, ";", NULL)) return;
		if (part->name[0] != '\0') {
			if_fail (send_smtp_strs(fd, "\tname=\"", part->name, "\"", NULL)) return;
		}
	}
	if_fail (send_smtp_strs(fd, "", NULL)) return;
	if_fail (send_file(fd, part->filename)) return;
}

static int send_forward(int fd, struct forward *fwd)
{
	int status;
	if_fail (status = smtp_cmd(fd, "MAIL FROM:<", fwd->from, ">", NULL)) return 0;
	if (status != 250) return status;
	if_fail (status = smtp_cmd(fd, "RCPT TO:<", fwd->to, ">", NULL)) return 0;
	if (status < 250 || status > 252) return status;
	if_fail (status = smtp_cmd(fd, "DATA", NULL)) return 0;
	if (status != 354) return status;
	if_fail (send_smtp_strs(fd, "From: ", fwd->from, NULL)) return 0;
	if_fail (send_smtp_strs(fd, "To: ", fwd->to, NULL)) return 0;
	if_fail (send_smtp_strs(fd, "Subject: ", fwd->subject, NULL)) return 0;
	if_fail (send_smtp_strs(fd, "Message-Id: <", mdir_version2str(fwd->version), "@", my_hostname, ">", NULL)) return 0;
	if_fail (send_smtp_strs(fd, "Mime-Version: 1.0", NULL)) return 0;
	char const *boundary = "Do-Noy-Cross-Criminal-Scene";
	if_fail (send_smtp_strs(fd, "Content-Type: multipart/mixed; boundary=", boundary, NULL)) return 0;
	struct part *part;
	TAILQ_FOREACH(part, &fwd->parts, entry) {
		if_fail (send_smtp_strs(fd, "", NULL)) return 0;
		if_fail (send_smtp_strs(fd, "--", boundary, NULL)) return 0;
		if_fail (send_part(fd, part)) break;
	}
	if_fail (send_smtp_strs(fd, "", NULL)) return 0;
	if_fail (send_smtp_strs(fd, "--", boundary, "--", NULL)) return 0;
	if_fail (status = smtp_cmd(fd, ".", NULL)) return 0;
	return status;
}

/*
 * Forwarder thread
 */

static bool transfers_done(struct forward *fwd)
{
	struct part *part;
	TAILQ_FOREACH(part, &fwd->parts, entry) {
		if (! part->tx) continue;
		int tx_status = chn_tx_status(part->tx);
		if (tx_status == 0) return false;
		if (tx_status != 200) {
			fwd->status = tx_status;
			return true;	// no need to wait further
		}
	}
	return true;
}

static void *forwarder(void *dummy)
{
	(void)dummy;
	struct forward *fwd, *tmp;
	while (! is_error() && ! terminate) {
		TAILQ_FOREACH_SAFE(fwd, &waiting_forwards, entry, tmp) {
			debug("considering waiting forward@%p", fwd);
			if (! transfers_done(fwd)) continue;	// may set fwd->status in case of error
			if (fwd->status == 0) {
				if_fail (may_open_connection()) goto q;	// if not already connected
				if_fail (fwd->status = send_forward(sock_fd, fwd)) {
					error_clear();	// will try later
					break;
				}
			}
			list_move_to(&delivered_forwards, fwd);
			sock_last_used = time(NULL);
		}
		may_close_connection();
		pth_sleep(1);	// TODO: a signal when this list becomes non empty
	}
q:	debug("Exiting forwarder thread");
	return NULL;
}

/*
 * Init
 */

void forwarder_begin(void)
{
	conf_set_default_str("SC_SMTP_RELAY_HOST", "localhost");
	conf_set_default_str("SC_SMTP_RELAY_PORT", "smtp");
	pth_mutex_init(&list_mutex);
	TAILQ_INIT(&waiting_forwards);
	TAILQ_INIT(&delivered_forwards);
	sock_fd = -1;
	if (0 != gethostname(my_hostname, sizeof(my_hostname))) with_error(errno, "gethostname") return;
	my_hostname[sizeof(my_hostname)-1] = '\0';
	forwarder_thread = pth_spawn(PTH_ATTR_DEFAULT, forwarder, NULL);
	if (forwarder_thread == NULL) with_error(0, "Cannot spawn forwarder") return;
}

void forwarder_end(void)
{
	if (forwarder_thread != NULL) {
		pth_abort(forwarder_thread);
		forwarder_thread = NULL;
	}
	list_empty(&waiting_forwards);
	list_empty(&delivered_forwards);
	if (sock_fd != -1) {
		(void)close(sock_fd);
		sock_fd = -1;
	}
}

/*
 * Forward & parts Ction/Dtion
 */

static void part_ctor(struct part *part, struct forward *fwd, char const *resource)
{
	// First get the resource values
	char resource_stripped[PATH_MAX];
	if_fail (header_stripped_value(resource, sizeof(resource_stripped), resource_stripped)) return;
	if_fail (header_copy_parameter("name", resource, sizeof(part->name), part->name)) {
		if (error_code() == ENOENT) {
			error_clear();
		} else return;
	}
	if_fail (header_copy_parameter("type", resource, sizeof(part->type), part->type)) {
		if (error_code() == ENOENT) {
			error_clear();
		} else return;
	}
	if_fail (part->tx = chn_get_file(&ccnx, part->filename, resource_stripped)) return;
	// we do not wait here for the transfert to complete, but will skip the delivery while the transfert is ongoing
	TAILQ_INSERT_TAIL(&fwd->parts, part, entry);
	fwd->nb_parts++;
}

static struct part *part_new(struct forward *fwd, char const *resource)
{
	struct part *part = malloc(sizeof(*part));
	if (! part) with_error(ENOMEM, "malloc part") return NULL;
	if_fail (part_ctor(part, fwd, resource)) {
		(void)free(part);
		part = NULL;
	}
	return part;
}

static void part_dtor(struct part *part, struct forward *fwd)
{
	TAILQ_REMOVE(&fwd->parts, part, entry);
	fwd->nb_parts--;
}

static void part_del(struct part *part, struct forward *fwd)
{
	part_dtor(part, fwd);
	free(part);
}

static void forward_ctor(struct forward *fwd, mdir_version version, char const *from, char const *to, char const *subject)
{
	debug("fwd@%p, '%s'=>'%s'", fwd, from, to);
	if_fail (fwd->from = Strdup(from)) return;
	if_fail (fwd->to = Strdup(to)) return;
	if_fail (fwd->subject = Strdup(subject)) return;
	TAILQ_INIT(&fwd->parts);
	fwd->nb_parts = 0;
	fwd->list = NULL;
	fwd->status = 0;
	fwd->version = version;
}

struct forward *forward_new(mdir_version version, char const *from, char const *to, char const *subject)
{
	struct forward *fwd = malloc(sizeof(*fwd));
	if (! fwd) with_error(ENOMEM, "malloc fwd") return NULL;
	if_fail (forward_ctor(fwd, version, from, to, subject)) {
		free(fwd);
		fwd = NULL;
	}
	return fwd;
}

static void forward_dtor(struct forward *fwd)
{
	debug("fwd@%p", fwd);
	list_lock();
	if (fwd->list) {
		TAILQ_REMOVE(fwd->list, fwd, entry);
		fwd->list = NULL;
	}
	list_unlock();
	FreeIfSet(&fwd->from);
	FreeIfSet(&fwd->to);
	FreeIfSet(&fwd->subject);
	struct part *part;
	while (NULL != (part = TAILQ_FIRST(&fwd->parts))) {
		part_del(part, fwd);
	}
	assert(fwd->nb_parts == 0);
}

void forward_del(struct forward *fwd)
{
	forward_dtor(fwd);
	free(fwd);
}

// Adding content & submition

void forward_part_new(struct forward *fwd, char const *resource)
{
	(void)part_new(fwd, resource);
}

void forward_submit(struct forward *fwd)
{
	list_move_to(&waiting_forwards, fwd);
	fwd->submited = time(NULL);
}

// Querrying

struct forward *forward_oldest_completed(void)
{
	return TAILQ_FIRST(&delivered_forwards);
}

