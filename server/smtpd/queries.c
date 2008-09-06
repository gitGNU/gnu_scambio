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
#include "header.h"
#include "mdir.h"

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

struct well_known_header well_known_headers[] = {
	{ "subject" },
	{ "message-id" },
	{ "content-type" },
	{ "content-transfer-encoding" },
};

/*
 * (De)Init
 */

int exec_begin(void)
{
	return 0;
}

void exec_end(void) {
}

/*
 * Answers
 */

static int answer_gen(struct cnx_env *env, int status, char const *cmpl, char sep)
{
	char line[100];
	size_t len = snprintf(line, sizeof(line), "%03d%c%s"CRLF, status, sep, cmpl);
	assert(len < sizeof(line));
	return Write(env->fd, line, len);
}
static int answer_cont(struct cnx_env *env, int status, char *cmpl)
{
	return answer_gen(env, status, cmpl, '-');
}
int answer(struct cnx_env *env, int status, char *cmpl)
{
	return answer_gen(env, status, cmpl, ' ');
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
	if (env->forward_path) free(env->forward_path);
	env->forward_path = strdup(forward_path);
}
static int greatings(struct cnx_env *env)
{
	return answer(env, OK, my_hostname);
}
int exec_ehlo(struct cnx_env *env, char const *domain)
{
	set_domain(env, domain);
	return greatings(env);
}
int exec_helo(struct cnx_env *env, char const *domain)
{
	set_domain(env, domain);
	return greatings(env);
}

/*
 * MAIL/RCPT
 */

// a mailbox is valid iff there exist a "mailboxes/name" mdir
static bool is_valid_mailbox(char const *name)
{
	static char mbox_dir[PATH_MAX] = "mailboxes/";	// TODO: make this configurable ?
#	define MB_LEN 10
	snprintf(mbox_dir+MB_LEN, sizeof(mbox_dir)-MB_LEN, "%s", name);
	return mdir_folder_exists(mbox_dir);
}

int exec_mail(struct cnx_env *env, char const *from)
{
	// RFC: In any event, a client MUST issue HELO or EHLO before starting a mail transaction.
	if (! env->domain) return answer(env, BAD_SEQUENCE, "What about presenting yourself first ?");
#	define FROM_LEN 5
	if (0 != strncasecmp("FROM:", from, FROM_LEN)) {
		return answer(env, SYNTAX_ERR, "I remember RFC mentioning 'MAIL FROM:...'");
	}
	set_reverse_path(env, from + FROM_LEN);
	return answer(env, OK, "Ok");
}
int exec_rcpt(struct cnx_env *env, char const *to)
{
	if (! env->reverse_path) return answer(env, BAD_SEQUENCE, "No MAIL command ?");
#	define TO_LEN 3
	if (0 != strncasecmp("TO:", to, TO_LEN)) {
		return answer(env, SYNTAX_ERR, "It should be 'RCPT TO:...', shouldn't it ?");
	}
	if (! is_valid_mailbox(to + TO_LEN)) {
		return answer(env, MBOX_UNAVAIL_2, "Bad guess");	// TODO: take action against this client ?
	}
	set_forward_path(env, to + TO_LEN);
	return answer(env, OK, "Ok");
}

/*
 * DATA
 */

static int store_file(struct cnx_env *env, struct header *header, struct varbuf *vb)
{
	(void)env;
	(void)header;
	(void)vb;
	// content is already decoded (by parse_mail)
	return -ENOSYS;
}

static int store_file_rec(struct cnx_env *env, struct msg_tree *const tree)
{
	if (tree->type == CT_FILE) {
		return store_file(env, tree->header, &tree->content.file);
	}
	assert(tree->type == CT_MULTIPART);
	struct msg_tree *subtree;
	SLIST_FOREACH(subtree, &tree->content.parts, entry) {
		int err = store_file_rec(env, subtree);
		if (err) return err;
	}
	return 0;
}

static int process_mail(struct cnx_env *env)
{
	// First parse the data into a mail tree
	struct msg_tree *msg_tree;
	int err = msg_tree_read(&msg_tree, env->fd);
	if (err) return err;
	// Extract the values that will be used for all meta-data blocs from top-level header
	env->subject = header_search(msg_tree->header, well_known_headers[WKH_SUBJECT].name);
	env->message_id = header_search(msg_tree->header, well_known_headers[WKH_MESSAGE_ID].name);
	// Then store each file in the mdir
	err = store_file_rec(env, msg_tree);
	msg_tree_del(msg_tree);
	return err;
}

int exec_data(struct cnx_env *env)
{
	int err;
	if (! env->forward_path) return answer(env, BAD_SEQUENCE, "What recipient, again ?");
	if (0 != (err = answer(env, START_MAIL, "Go ahaid, end with a single dot"))) return err;
	if (0 != (err = process_mail(env))) {
		return answer(env, LOCAL_ERROR, "Something bad happened at some point");
	}
	return answer(env, OK, "Ok");
}

/*
 * Other commands
 */

int exec_rset(struct cnx_env *env)
{
	reset_state(env);
	return answer(env, OK, "Ok");
}
int exec_vrfy(struct cnx_env *env, char const *user)
{
	(void)user;
	return answer(env, LOCAL_ERROR, "Not implemented yet");
}
int exec_expn(struct cnx_env *env, char const *list)
{
	(void)list;
	return answer(env, LOCAL_ERROR, "Not implemented yet");
}
int exec_help(struct cnx_env *env, char const *command)
{
	(void)command;
	return answer(env, LOCAL_ERROR, "Not implemented yet");
}
int exec_noop(struct cnx_env *env)
{
	return answer(env, OK, "Ok");
}
int exec_quit(struct cnx_env *env)
{
	return answer(env, SERVICE_CLOSING, "Bye");
}

