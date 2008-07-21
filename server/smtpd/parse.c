#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "scambio.h"
#include "smtpd.h"
#include "header.h"
#include "misc.h"

#ifndef HAVE_STRNSTR
static char *strnstr(const char *big_, const char *little, size_t big_len)
{
	char *big = (char *)big_;	// hare we have non-const strings
	assert(big_len >= strlen(big));
	char old_val = big[big_len];
	big[big_len] = '\0';
	char *ret = strstr(big, little);
	big[big_len] = old_val;
	return ret;
}
#endif

/*
 * Parse a mail from a file descriptor into a struct msg_tree
 */

static int parse_mail_rec(struct msg_tree **node, char *msg, size_t size);

// We treat the preamble (what preceeds the first boundary) as a normal part.
// The epilogue is ignored, though.
static int parse_multipart(struct msg_tree *node, char *msg, size_t size, char *boundary)
{
	char *msg_end = msg+size;
	node->type = CT_MULTIPART;
	SLIST_INIT(&node->content.parts);
	size_t const boundary_len = strlen(boundary);
	bool last_boundary = true;
	do {
		char *delim_pos = strnstr(msg, boundary, msg_end-msg);
		if (! delim_pos) {
			warning("multipart boundary not found (%s)", boundary);
			return -ENOENT;
		}
		assert(delim_pos < msg_end);
		struct msg_tree *part;
		if (0 == parse_mail_rec(&part, msg, delim_pos - msg)) {
			SLIST_INSERT_HEAD(&node->content.parts, part, entry);
		}
		msg = delim_pos + boundary_len;
		// boundary may be followed by "--" if its the last one, then some optional spaces then CRLF.
		if (msg[0] == '-' && msg[1] == '-') {
			msg += 2;
			last_boundary = true;
		}
		while (isblank(*msg)) msg++;
		if (*msg != '\n') {	// CRLF are converted to '\n' by varbuf_read_line()
			warning("Badly formated multipart message : boundary not followed by new line (%s)", boundary);
			return -EINVAL;
		}
		msg ++;	// skip NL
	} while (msg < msg_end && !last_boundary);
	return 0;
}

static int parse_mail_node(struct msg_tree *node, char *msg, size_t size)
{
	// First read the header
	node->header = header_new(msg);
	if (! node->header) return -EINVAL;	// TODO FIX header_new
	// Find out weither the body is total with a decoding method, or
	// is another message up to a given boundary.
	char const *content_type = header_search(node->header,
		well_known_headers[WKH_CONTENT_TYPE].name, well_known_headers[WKH_CONTENT_TYPE].key);
	if (content_type && 0 == strncasecmp(content_type, "multipart/", 10)) {
		debug("message is multipart");
#		define PREFIX "\n--"
#		define PREFIX_LENGTH 3
		char boundary[PREFIX_LENGTH+MAX_BOUNDARY_LENGTH] = PREFIX;
		int err;
		if (0 > (err = header_copy_parameter("boundary", content_type, sizeof(boundary)-PREFIX_LENGTH, boundary+PREFIX_LENGTH))) {
			warning("multipart message without boundary ? : %s", strerror(-err));	// proceed as a single file
		} else {
			return parse_multipart(node, msg, size, boundary);
		}
	}
	// Process mail as a single file
	node->type = CT_FILE;
	debug("message is a single file");
	return -ENOSYS;
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
static int parse_mail_rec(struct msg_tree **node, char *msg, size_t size)
{
	*node = calloc(1, sizeof(**node));
	if (! *node) return -ENOMEM;
	int err;
	if (0 != (err = parse_mail_node(*node, msg, size))) {
		msg_tree_del(*node);
		*node = NULL;
	}
	return err;
}

static int read_whole_mail(struct varbuf *vb, int fd)
{
	debug("read_whole_mail(vb=%p, fd=%d)", vb, fd);
	int err = 0;
	char *line;
	while (0 == (err = varbuf_read_line(vb, fd, MAX_MAILLINE_LENGTH, &line))) {
		if (! line_match(line, ".")) continue;
		// chop this line
		vb->used = line - vb->buf + 1;
		vb->buf[vb->used-1] = '\0';
		break;
	}
	if (err == 1) {
		err = -EINVAL;	// we are not supposed to encounter EOF while transfering a MAIL
	}
	return err;
}

int msg_tree_read(struct msg_tree **root, int fd)
{
	int err;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 10240, true))) return err;
	err = read_whole_mail(&vb, fd);
	if (! err) err = parse_mail_rec(root, vb.buf, vb.used);
	varbuf_dtor(&vb);
	return err;
}

