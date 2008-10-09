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
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pth.h>
#include "scambio.h"
#include "smtpd.h"
#include "misc.h"
#include "varbuf.h"
#include "scambio/header.h"
#include "scambio/mdir.h"

// Errors from RFC
#define SYSTEM_REPLY    211 // System status, or system help reply
#define HELP_MESSAGE    214 // Information on how to use the receiver or the meaning of a particular non-standard command; this reply is useful only to the human user
#define SERVICE_READY   220 // followed by <domain>
#define SERVICE_CLOSING 221 // followed by <domain>
#define OK              250 // Requested mail action okay, completed
#define NOT_LOCAL       251 // User not local; will forward to <forward-path> (See section 3.4)
#define CANNOTT_VRFY    252 // Cannot VRFY user, but will accept message and attempt delivery (See section 3.5.3)
#define START_MAIL      354 // Start mail input; end with <CRLF>.<CRLF>
#define SERVICE_UNAVAIL 421 // <domain> Service not available, closing transmission channel (This may be a reply to any command if the service knows it must shut down)
#define MBOX_UNAVAIL    450 // Requested mail action not taken: mailbox unavailable (e.g., mailbox busy)
#define LOCAL_ERROR     451 // Requested action aborted: local error in processing
#define SHORT_STORAGE   452 // Requested action not taken: insufficient system storage
#define SYNTAX_ERR      500 // Syntax error, command unrecognized (This may include errors such as command line too long)
#define SYNTAX_ERR_PARM 501 // Syntax error in parameters or arguments
#define NOT_IMPLEMENTED 502 // Command not implemented (see section 4.2.4)
#define BAD_SEQUENCE    503 // Bad sequence of commands
#define PARM_NOT_IMPL   504 // Command parameter not implemented
#define MBOX_UNAVAIL_2  550 // Requested action not taken: mailbox unavailable (e.g., mailbox not found, no access, or command rejected for policy reasons)
#define NOT_LOCAL_X     551 // User not local; please try <forward-path> (See section 3.4)
#define NO_MORE_STORAGE 552 // Requested mail action aborted: exceeded storage allocation
#define MBOX_UNALLOWED  553 // Requested action not taken: mailbox name not allowed (e.g., mailbox syntax incorrect)
#define TRANSAC_FAILED  554 // Transaction failed  (Or, in the case of a connection-opening response, "No SMTP service here")

/*
 * (De)Init
 */

void exec_begin(void)
{
}

void exec_end(void) {
}

/*
 * Answers
 */

static void answer_gen(struct cnx_env *env, int status, char const *cmpl, char sep)
{
	char line[100];
	size_t len = snprintf(line, sizeof(line), "%03d%c%s"CRLF, status, sep, cmpl);
	assert(len < sizeof(line));
	Write(env->fd, line, len);
}
#if 0
static void answer_cont(struct cnx_env *env, int status, char *cmpl)
{
	answer_gen(env, status, cmpl, '-');
}
#endif
void answer(struct cnx_env *env, int status, char *cmpl)
{
	answer_gen(env, status, cmpl, ' ');
}

/*
 * HELO
 */

static void reset_state(struct cnx_env *env)
{
	if (env->domain) {
		free(env->domain);
		env->domain = NULL;
	}
	if (env->reverse_path) {
		free(env->reverse_path);
		env->reverse_path = NULL;
	}
	if (env->forward_path) {
		free(env->forward_path);
		env->forward_path = NULL;
	}
}
static void set_domain(struct cnx_env *env, char const *id)
{
	reset_state(env);
	env->domain = strdup(id);
}
static void set_reverse_path(struct cnx_env *env, char const *reverse_path)
{
	if (env->reverse_path) free(env->reverse_path);
	env->reverse_path = strdup(reverse_path);
}
static void set_forward_path(struct cnx_env *env, char const *forward_path)
{
	char folder[PATH_MAX];
	snprintf(folder, sizeof(folder), "mailboxes/%s", forward_path);
	env->mailbox = mdir_lookup(folder);
	on_error return;
	if (env->forward_path) free(env->forward_path);
	env->forward_path = strdup(forward_path);
}
static void greatings(struct cnx_env *env)
{
	answer(env, OK, my_hostname);
}
void exec_ehlo(struct cnx_env *env, char const *domain)
{
	set_domain(env, domain);
	greatings(env);
}
void exec_helo(struct cnx_env *env, char const *domain)
{
	set_domain(env, domain);
	greatings(env);
}

/*
 * MAIL/RCPT
 */

void exec_mail(struct cnx_env *env, char const *from)
{
#	define FROM_LEN 5
	// RFC: In any event, a client MUST issue HELO or EHLO before starting a mail transaction.
	if (! env->domain) {
		answer(env, BAD_SEQUENCE, "What about presenting yourself first ?");
	} else if (0 != strncasecmp("FROM:", from, FROM_LEN)) {
		answer(env, SYNTAX_ERR, "I remember RFC mentioning 'MAIL FROM:...'");
	} else {
		set_reverse_path(env, from + FROM_LEN);
		answer(env, OK, "Ok");
	}
}

void exec_rcpt(struct cnx_env *env, char const *to)
{
#	define TO_LEN 3
	if (! env->reverse_path) {
		answer(env, BAD_SEQUENCE, "No MAIL command ?");
	} else if (0 != strncasecmp("TO:", to, TO_LEN)) {
		answer(env, SYNTAX_ERR, "It should be 'RCPT TO:...', shouldn't it ?");
	} else {
		char const *name = to + TO_LEN;
		while (*name != '\0' && *name == ' ') name++;	// some client may insert blank spaces here, although it's forbidden
		set_forward_path(env, name);
		on_error {
			answer(env, MBOX_UNAVAIL_2, "Bad guess");	// TODO: take action against this client ?
			error_clear();
		} else {
			answer(env, OK, "Ok");
		}
	}
}

/*
 * DATA
 */

static void store_file(struct cnx_env *env, struct header *header, struct varbuf *vb)
{
	(void)vb;
	(void)env;
	(void)header;
	// TODO: send the content to the file store and add returned URL to the global header
}

static void store_file_rec(struct cnx_env *env, struct msg_tree *const tree)
{
	if (tree->type == CT_FILE) {
		store_file(env, tree->header, &tree->content.file);
		return;
	}
	assert(tree->type == CT_MULTIPART);
	struct msg_tree *subtree;
	SLIST_FOREACH(subtree, &tree->content.parts, entry) {
		if_fail (store_file_rec(env, subtree)) return;
	}
}

static void submit_patch(struct cnx_env *env)
{
	struct header *h = header_new();
	on_error return;
	header_add_field(h, SCAMBIO_TYPE_FIELD, SCAMBIO_MAIL_TYPE);
	header_add_field(h, SCAMBIO_FROM_FIELD, env->reverse_path);
	if (env->subject) header_add_field(h, SCAMBIO_DESCR_FIELD, env->subject);
	unless_error mdir_patch_request(env->mailbox, MDIR_ADD, h);
	header_del(h);
}

static void process_mail(struct cnx_env *env)
{
	// First parse the data into a mail tree
	struct msg_tree *msg_tree = msg_tree_read(env->fd);
	on_error return;
	// Extract the values that will be used for all meta-data blocs from top-level header
	env->subject = header_search(msg_tree->header, "subject");
	env->message_id = header_search(msg_tree->header, "message-id");
	// Then store each file in the mdir
	store_file_rec(env, msg_tree);
	submit_patch(env);
	msg_tree_del(msg_tree);
}

void exec_data(struct cnx_env *env)
{
	if (! env->forward_path) {
		answer(env, BAD_SEQUENCE, "What recipient, again ?");
	} else {
		if_fail (answer(env, START_MAIL, "Go ahaid, end with a single dot")) return;
		if_succeed (process_mail(env)) {
			answer(env, OK, "Ok");
		} else {
			answer(env, LOCAL_ERROR, "Something bad happened at some point");
		}
	}
}

/*
 * Other commands
 */

void exec_rset(struct cnx_env *env)
{
	reset_state(env);
	answer(env, OK, "Ok");
}
void exec_vrfy(struct cnx_env *env, char const *user)
{
	(void)user;
	answer(env, LOCAL_ERROR, "Not implemented yet");
}
void exec_expn(struct cnx_env *env, char const *list)
{
	(void)list;
	answer(env, LOCAL_ERROR, "Not implemented yet");
}
void exec_help(struct cnx_env *env, char const *command)
{
	(void)command;
	answer(env, LOCAL_ERROR, "Not implemented yet");
}
void exec_noop(struct cnx_env *env)
{
	answer(env, OK, "Ok");
}
void exec_quit(struct cnx_env *env)
{
	answer(env, SERVICE_CLOSING, "Bye");
}

