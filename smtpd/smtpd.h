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
#ifndef SMTPD_H_080623
#define SMTPD_H_080623

#include <pth.h>
#include "scambio/queue.h"
#include "scambio/cnx.h"
#include "scambio/channel.h"
#include "varbuf.h"

#define CRLF "\r\n"
#define MAX_MAILLINE_LENGTH 1000
/* Boundary delimiters must not appear within the encapsulated material,
 * and must be no longer than 70 characters, not counting the two leading
 * hyphens. -- RFC 2046
 */
#define MAX_BOUNDARY_LENGTH 70

extern char my_hostname[256];
extern struct chn_cnx ccnx;

struct mdir;
struct cnx_env {
	struct mdir_cnx cnx;
	bool quit;
	char *domain;	// stores the last received helo information
	char *reverse_path;
	char *forward_path;
	struct mdir *mailbox;	// associated to the forward path
	char client_address[100];
	char reception_date[10+1];
	char reception_time[8+1];
};

// From queries.c

void exec_begin(void);
void exec_end(void);
mdir_cmd_cb exec_helo, exec_mail, exec_rcpt, exec_data, exec_rset, exec_vrfy, exec_expn, exec_help, exec_noop, exec_quit;

// From parse.c

struct header;
struct msg_tree {
	struct header *header;
	enum msg_tree_type { CT_NONE=0, CT_FILE, CT_MULTIPART } type;
	union {
		struct {
			struct varbuf data;
			char params[128];	// wild guess
		} file;
		SLIST_HEAD(subtrees, msg_tree) parts;
	} content;
	SLIST_ENTRY(msg_tree) entry;
};

struct msg_tree;
struct msg_tree *msg_tree_read(int fd);
void msg_tree_del(struct msg_tree *);

#endif
