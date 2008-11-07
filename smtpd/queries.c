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
void exec_helo(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct cnx_env *env = DOWNCAST(cnx, cnx, cnx_env);
	set_domain(env, cmd->args[0].string);
	mdir_cnx_answer(cnx, cmd, OK, my_hostname);
}

/*
 * MAIL/RCPT
 */

void exec_mail(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct cnx_env *env = DOWNCAST(cnx, cnx, cnx_env);
	char const *const from = cmd->args[0].string;
#	define FROM_LEN 5
	// RFC: In any event, a client MUST issue HELO or EHLO before starting a mail transaction.
	if (! env->domain) {
		mdir_cnx_answer(cnx, cmd, BAD_SEQUENCE, "What about presenting yourself first ?");
	} else if (0 != strncasecmp("FROM:", from, FROM_LEN)) {
		mdir_cnx_answer(cnx, cmd, SYNTAX_ERR, "I remember RFC mentioning 'MAIL FROM:...'");
	} else {
		set_reverse_path(env, from + FROM_LEN);
		mdir_cnx_answer(&env->cnx, cmd, OK, "Ok");
	}
}

void exec_rcpt(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct cnx_env *env = DOWNCAST(cnx, cnx, cnx_env);
	char const *const to = cmd->args[0].string;
#	define TO_LEN 3
	if (! env->reverse_path) {
		mdir_cnx_answer(cnx, cmd, BAD_SEQUENCE, "No MAIL command ?");
	} else if (0 != strncasecmp("TO:", to, TO_LEN)) {
		mdir_cnx_answer(cnx, cmd, SYNTAX_ERR, "It should be 'RCPT TO:...', shouldn't it ?");
	} else {
		char const *name = to + TO_LEN;
		while (*name != '\0' && *name == ' ') name++;	// some client may insert blank spaces here, although it's forbidden
		set_forward_path(env, name);
		on_error {
			mdir_cnx_answer(cnx, cmd, MBOX_UNAVAIL_2, "Bad guess");	// TODO: take action against this client ?
			error_clear();
		} else {
			mdir_cnx_answer(cnx, cmd, OK, "Ok");
		}
	}
}

/*
 * DATA
 */

static void send_varbuf(struct chn_cnx *cnx, char const *resource, struct varbuf *vb)
{
	// Do it a temp file, sends the file, removes the file !
	char tmpfile[PATH_MAX] = "/tmp/vbXXXXXX";	// FIXME
	int fd = mkstemp(tmpfile);
	if (fd == -1) with_error(errno, "mkstemp(%s)", tmpfile) return;
	if (0 != unlink(tmpfile)) {
		error("Cannot unlink tmp file '%s' : %s", tmpfile, strerror(errno));
		// go ahaid
	}
	do {
		if_fail(varbuf_write(vb, fd)) break;
		chn_send_file(cnx, resource, fd);
	} while (0);
	(void)close(fd);
}

static void store_file(struct header *header, struct varbuf *vb, struct header *global_header)
{
	char resource[PATH_MAX];
	debug("Creating a resource for");
	header_debug(header);
	if_fail (chn_create(&ccnx, resource, false)) return;
	debug("Obtained resource '%s', now upload it", resource);
	if_fail (send_varbuf(&ccnx, resource, vb)) return;
	debug("That's great, now adding this info onto the env header");
	if_fail (header_add_field(global_header, SC_RESOURCE_FIELD, resource)) return;
}

static void store_file_rec(struct msg_tree *const tree, struct header *global_header)
{
	if (tree->type == CT_FILE) {
		store_file(tree->header, &tree->content.file, global_header);
		return;
	}
	assert(tree->type == CT_MULTIPART);
	struct msg_tree *subtree;
	SLIST_FOREACH(subtree, &tree->content.parts, entry) {
		if_fail (store_file_rec(subtree, global_header)) return;
	}
}

static void process_mail(struct cnx_env *env)
{
	// First parse the data into a mail tree
	struct msg_tree *msg_tree = msg_tree_read(env->cnx.fd);
	on_error return;
	do {
		struct header *h = header_new();
		on_error break;
		do {
			if_fail (header_add_field(h, SC_TYPE_FIELD, SC_MAIL_TYPE)) break;
			if_fail (header_add_field(h, SC_FROM_FIELD, env->reverse_path)) break;
			// Store each file in the filed
			if_fail (store_file_rec(msg_tree, h)) break;
			char const *subject = header_search(msg_tree->header, "subject");
			if (subject) header_add_field(h, SC_DESCR_FIELD, subject);
			char const *message_id = header_search(msg_tree->header, "message-id");
			if (message_id) header_add_field(h, SC_EXTID_FIELD, message_id);
			// submit the header
			if_fail (mdir_patch_request(env->mailbox, MDIR_ADD, h)) break;
		} while (0);
		header_del(h);
	} while (0);
	msg_tree_del(msg_tree);
}

void exec_data(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct cnx_env *env = DOWNCAST(cnx, cnx, cnx_env);
	if (! env->forward_path) {
		mdir_cnx_answer(cnx, cmd, BAD_SEQUENCE, "What recipient, again ?");
	} else {
		if_fail (mdir_cnx_answer(cnx, cmd, START_MAIL, "Go ahaid, end with a single dot")) return;
		if_succeed (process_mail(env)) {
			mdir_cnx_answer(cnx, cmd, OK, "Ok");
		} else {
			mdir_cnx_answer(cnx, cmd, LOCAL_ERROR, "Something bad happened at some point");
		}
	}
}

/*
 * Other commands
 */

void exec_rset(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct cnx_env *env = DOWNCAST(cnx, cnx, cnx_env);
	reset_state(env);
	mdir_cnx_answer(cnx, cmd, OK, "Ok");
}

void exec_vrfy(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	mdir_cnx_answer(cnx, cmd, LOCAL_ERROR, "Not implemented yet");
}

void exec_expn(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	mdir_cnx_answer(cnx, cmd, LOCAL_ERROR, "Not implemented yet");
}

void exec_help(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	mdir_cnx_answer(cnx, cmd, LOCAL_ERROR, "Not implemented yet");
}

void exec_noop(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	mdir_cnx_answer(cnx, cmd, OK, "Ok");
}

void exec_quit(struct mdir_cmd *cmd, void *user_data)
{
	struct mdir_cnx *const cnx = user_data;
	struct cnx_env *env = DOWNCAST(cnx, cnx, cnx_env);
	env->quit = true;
	mdir_cnx_answer(cnx, cmd, SERVICE_CLOSING, "Bye");
}

