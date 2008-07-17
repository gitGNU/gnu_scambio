#include "scambio.h"
#include "smtpd.h"

/*
 * Parse a mail from a file descriptor into a struct msg_tree
 */

static int parse_mail_rec(struct msg_tree **root, struct varbuf *vb)
{
	// First read the header
	// Then the body, which may be a mere unstructured body or another message up to a boundary
	// TODO
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
	if (! err) err = parse_mail_rec(root, &vb);
	varbuf_dtor(&vb);
	return err;
}

