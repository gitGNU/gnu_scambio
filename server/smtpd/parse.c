#include "scambio.h"
#include "smtpd.h"

/*
 * Parse a mail from a file descriptor into a struct msg_tree
 */

static int parse_mail_rec(struct msg_tree **root, struct varbuf *vb)
{

}

static int read_whole_mail(struct varbuf *vb, int fd)
{

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

