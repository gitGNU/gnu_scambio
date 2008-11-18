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
#include <assert.h>
#include <ctype.h>
#include "scambio.h"
#include "smtpd.h"
#include "misc.h"
#include "scambio/header.h"

#ifndef HAVE_STRNSTR
static char *strnstr(const char *big_, const char *little, size_t max_len)
{
	char *big = (char *)big_;	// here we have non-const strings
	assert(max_len <= strlen(big));
	char old_val = big[max_len];
	big[max_len] = '\0';
	debug("looking for %s in %s", little, big);
	char *ret = strstr(big, little);
	debug("found @ %p", ret);
	big[max_len] = old_val;
	return ret;
}
#endif

/*
 * Parse a mail from a file descriptor into a struct msg_tree
 */

static struct msg_tree *parse_mail_rec(char *msg, size_t size);

// We ignore preamble and epilogue.
static void parse_multipart(struct msg_tree *node, char *msg, size_t size, char *boundary)
{
	char *msg_end = msg+size;
	node->type = CT_MULTIPART;
	SLIST_INIT(&node->content.parts);
	size_t const boundary_len = strlen(boundary);
	bool last_boundary = false;
	bool first_boundary = true;
	// Go straight to the first boundary for first component
	do {
		char *delim_pos = strnstr(msg, boundary, msg_end-msg);
		if (! delim_pos) {
			with_error(0, "multipart boundary not found (%s)", boundary) return;
		}
		assert(delim_pos < msg_end);
		if (! first_boundary) {
			struct msg_tree *part = parse_mail_rec(msg, delim_pos - msg);
			unless_error SLIST_INSERT_HEAD(&node->content.parts, part, entry);
		}
		first_boundary = false;
		msg = delim_pos + boundary_len;
		// boundary may be followed by "--" if it's the last one, then some optional spaces then CRLF.
		if (msg[0] == '-' && msg[1] == '-') {
			msg += 2;
			last_boundary = true;
		}
		while (isblank(*msg)) msg++;
		if (*msg != '\n') {	// CRLF are converted to '\n' by varbuf_read_line()
			with_error(0, "Badly formated multipart message : boundary not followed by new line (%s)", boundary) return;
		}
		msg ++;	// skip NL
	} while (msg < msg_end && !last_boundary);
}

static void parse_mail_node(struct msg_tree *node, char *msg, size_t size)
{
	// First read the header
	if_fail (node->header = header_new()) return;
	size_t header_size;
	if_fail (header_size = header_parse(node->header, msg)) {
		header_del(node->header);
		return;
	}
	assert(header_size <= size);
	// Find out weither the body is total with a decoding method, or
	// is another message up to a given boundary.
	char const *content_type = header_search(node->header, "content-type");
	if (content_type && 0 == strncasecmp(content_type, "multipart/", 10)) {
		debug("message is multipart");
#		define PREFIX "\n--"
#		define PREFIX_LENGTH 3
		char boundary[PREFIX_LENGTH+MAX_BOUNDARY_LENGTH] = PREFIX;
		if_succeed (header_copy_parameter("boundary", content_type, sizeof(boundary)-PREFIX_LENGTH, boundary+PREFIX_LENGTH)) {
			parse_multipart(node, msg, size, boundary);
			return;
		} else {
			warning("multipart message without boundary ?");	// proceed as a single file
		}
	}
	// Process mail as a single file
	debug("message is a single file");
	// read the file in node->content.file
	if_fail (varbuf_ctor(&node->content.file, 1024, true)) return;
	node->type = CT_FILE;
	if (msg[header_size] == '\n') header_size++;	// A SMTP header is supposed to be ended because of en empty line that we dont want on the file
	if_fail (varbuf_append(&node->content.file, size-header_size, msg+header_size)) return;
}

static void msg_tree_dtor(struct msg_tree *node)
{
	switch (node->type) {
		case CT_NONE:
			break;
		case CT_FILE:
			varbuf_dtor(&node->content.file);
			break;
		case CT_MULTIPART:
			{
				struct msg_tree *sub;
				SLIST_FOREACH(sub, &node->content.parts, entry) {
					msg_tree_del(sub);
				}
			}
			break;
	}
}

void msg_tree_del(struct msg_tree *node)
{
	msg_tree_dtor(node);
	free(node);
}

// do not look msg after size
static struct msg_tree *parse_mail_rec(char *msg, size_t size)
{
	struct msg_tree *node = calloc(1, sizeof(*node));
	if (! node) with_error(ENOMEM, "calloc mail") return NULL;
	if_fail (parse_mail_node(node, msg, size)) {
		msg_tree_del(node);
		node = NULL;
	}
	return node;
}

static void read_whole_mail(struct varbuf *vb, int fd)
{
	debug("read_whole_mail(vb=%p, fd=%d)", vb, fd);
	char *line;
	do {
		varbuf_read_line(vb, fd, MAX_MAILLINE_LENGTH, &line);
		on_error break;
		if (line_match(line, ".")) {	// chop this line
			vb->used = line - vb->buf + 1;
			vb->buf[vb->used-1] = '\0';
			break;
		}
	} while (1);
}

struct msg_tree *msg_tree_read(int fd)
{
	struct msg_tree *root = NULL;
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, 10240, true)) return NULL;
	read_whole_mail(&vb, fd);
	unless_error root = parse_mail_rec(vb.buf, vb.used -1 /*trailling 0 is not to be considered*/);
	varbuf_dtor(&vb);
	return root;
}

