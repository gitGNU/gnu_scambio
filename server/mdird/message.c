#include <stdbool.h>
#include "message.h"
#include "log.h"

/*
 * URL
 */

#define MAX_URL_LENGTH 8000
#define MAX_HEADLINE_LENGTH 10000
#define MAX_HEADER_LINES 300

static bool url_is_valid(struct varbuf *vb)
{
	size_t const len = vb->used;
	return len > 0 && len < MAX_URL_LENGTH;
}

int read_url(struct varbuf *vb, int fd)
{
	int err;
	if (0 <= (err = varbuf_read_line(vb, fd, MAX_URL_LENGTH))) return err;
	if (! url_is_valid(vb)) {
		varbuf_clean(vb);
		return -1;
	}
	return 0;
}

/*
 * Headers
 */

int read_header(struct varbuf *vb, int fd)
{
	int err = 0;
	int nb_lines = 0;
	while (0 < (err = varbuf_read_line(vb, fd, MAX_HEADLINE_LENGTH))) {
		if (++ nb_lines > MAX_HEADER_LINES) {
			err = -E2BIG;
			break;
		}
	}
	if (nb_lines == 0) {
		err = -EINVAL;
	}
	if (err < 0) {
		varbuf_clean(vb);
	}
	return err;
}

void insert_message(struct varbuf *vb, struct header *head)
{
	debug("insert message ... TODO");
}

