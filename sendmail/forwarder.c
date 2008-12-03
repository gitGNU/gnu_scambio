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
#include <pth.h>
#include "scambio.h"
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
static time_t sock_opened_at;
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

static struct forward *list_first(struct forwards *list)
{
	return TAILQ_FIRST(list);
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
	sock_opened_at = time(NULL);
}

static void may_close_connection(void)
{
	if (sock_fd == -1) return;
	unsigned delay = time(NULL) - sock_opened_at;
	if (delay > CNX_IDLE_TIMEOUT) {
		debug("Closing idle SMTP cnx");
		(void)smtp_cmd(sock_fd, "QUIT", NULL);
		error_clear();
		(void)close(sock_fd);
		sock_fd = -1;
	}
}

static int send_forward(struct forward *fwd)
{
	(void)fwd;
	int status;
	if_fail (status = smtp_cmd(sock_fd, "MAIL FROM:<", fwd->from, ">", NULL)) return 0;
	if (status != 250) return status;
	if_fail (status = smtp_cmd(sock_fd, "RCPT TO:<", fwd->to, ">", NULL)) return 0;
	if (status < 250 || status > 252) return status;
	if_fail (status = smtp_cmd(sock_fd, "DATA", NULL)) return 0;
	if (status != 354) return status;
	// TODO
	if_fail (send_smtp_strs(sock_fd, "From: ", fwd->from, NULL)) return 0;
	if_fail (send_smtp_strs(sock_fd, "To: ", fwd->to, NULL)) return 0;
	if_fail (send_smtp_strs(sock_fd, "Subject: ", fwd->subject, NULL)) return 0;
	if_fail (send_smtp_strs(sock_fd, "", NULL)) return 0;
	if_fail (send_smtp_strs(sock_fd, "Scambio is fun !", NULL)) return 0;
	if_fail (status = smtp_cmd(sock_fd, ".", NULL)) return 0;
	return status;
}

/*
 * Forwarder thread
 */

static void *forwarder(void *dummy)
{
	(void)dummy;
	struct forward *fwd;
	while (! is_error() && ! terminate) {
		while (NULL != (fwd = list_first(&waiting_forwards))) {
			if_fail (may_open_connection()) goto q;	// if not already connected
			if_fail (fwd->status = send_forward(fwd)) {
				error_clear();	// will try later
				break;
			}
			list_move_to(&delivered_forwards, fwd);
			sock_opened_at = time(NULL);
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
 * Forward
 */

// Ction/Dtion

static void forward_ctor(struct forward *fwd, mdir_version version, char const *from, char const *to, char const *subject)
{
	if_fail (fwd->from = Strdup(from)) return;
	if_fail (fwd->to = Strdup(to)) return;
	if_fail (fwd->subject = Strdup(subject)) return;
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
	list_lock();
	if (fwd->list) {
		TAILQ_REMOVE(fwd->list, fwd, entry);
		fwd->list = NULL;
	}
	list_unlock();
	FreeIfSet(&fwd->from);
	FreeIfSet(&fwd->to);
	FreeIfSet(&fwd->subject);
}

void forward_del(struct forward *fwd)
{
	forward_dtor(fwd);
	free(fwd);
}

// Adding content & submition

void forward_part_new(struct forward *fwd, char const *resource)
{
	(void)fwd;
	(void)resource;
}

void forward_submit(struct forward *fwd)
{
	list_move_to(&waiting_forwards, fwd);
	fwd->submited = time(NULL);
}

// Querrying

struct forward *forward_oldest_completed(void)
{
	return list_first(&delivered_forwards);
}

