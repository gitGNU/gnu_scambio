#include "scambio.h"
#include "smtpd.h"

/*
 * Parse a mail from a file descriptor into a struct msg_tree
 */

static int parse_mail_rec(struct msg_tree **node, char *msg, char const *delimiter);
static int parse_mail_node(struct msg_tree *node, char *msg, char const *delimiter)
{
	// First read the header
	node->header = header_new(msg);
	if (! node->header) return -EINVAL;
	// Find out weither the body is total with a decoding method, or
	// is another message up to a given boundary.
	char const *content_type = header_search(node->header,
		well_known_headers[WKH_CONTENT_TYPE].name, well_known_headers[WKH_CONTENT_TYPE].key);
	if (content_type && 0 == strncasecmp(content_type, "multipart/", 10)) {
		node->type = CT_MULTIPART;
		SLIST_INIT(&node->content.parts);

	} else {
		node->type = CT_FILE;
	}
}

static int parse_mail_rec(struct msg_tree **node, char *msg, char const *delimiter)
{
	*node = malloc(sizeof(**node));
	if (! *node) return -ENOMEM;
	int err;
	if (0 != (err = parse_mail_node(*node, msg, delimiter))) {
		free(*node);
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
	if (! err) err = parse_mail_rec(root, vb->buf, NULL);
	varbuf_dtor(&vb);
	return err;
}

