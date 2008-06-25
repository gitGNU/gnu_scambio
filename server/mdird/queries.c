#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <pth.h>
#include "scambio.h"
#include "mdird.h"
#include "misc.h"
#include "varbuf.h"
#include "jnl.h"
#include "header.h"

/*
 * Misc
 */

// a line is said to match a delim if it starts with the delim, and is followed only by optional spaces
static bool line_match(char *restrict line, char *restrict delim)
{
	unsigned c = 0;
	while (delim[c] != '\0') {
		if (delim[c] != line[c]) return false;
		c++;
	}
	while (line[c] != '\0') {
		if (! isspace(line[c])) return false;
		c++;
	}
	return true;
}

// Reads fd until a "%%" line
static int read_header(struct varbuf *vb, int fd)
{
	int err = 0;
	int nb_lines = 0;
	char *line;
	bool eoh_reached = false;
	while (0 < (err = varbuf_read_line(vb, fd, MAX_HEADLINE_LENGTH, &line))) {
		if (++ nb_lines > MAX_HEADER_LINES) {
			err = -E2BIG;
			break;
		}
		if (line_match(line, "%%")) {
			// forget this line
			vb->used = line - vb->buf + 1;
			vb->buf[vb->used-1] = '\0';
			eoh_reached = true;
			break;
		}
	}
	if (nb_lines == 0) err = -EINVAL;
	if (! eoh_reached) err = -EINVAL;
	if (err < 0) {
		varbuf_clean(vb);
	}
	return err;
}

/*
 * Answers
 */

static int answer(struct cnx_env *env, long long seq, char const *cmd_name, int status, char const *cmpl)
{
	char reply[100];
	size_t len = snprintf(reply, sizeof(reply), "%lld %s %d (%s)\n", seq, cmd_name, status, cmpl);
	assert(len < sizeof(reply));
	return Write(env->fd, reply, len);
}

/*
 * DIFF
 */

int exec_diff(struct cnx_env *env, long long seq, char const *dir, long long version)
{
	debug("doing DIFF for '%s' from version %lld", dir, version);
	return answer(env, seq, "DIFF", 500, "OK");
}

/*
 * PUT
 */

int exec_put(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing PUT in '%s'", dir);
	struct varbuf vb;
	int err = varbuf_ctor(&vb, 10000, true);
	if (! err) {
		err = read_header(&vb, env->fd);
		if (err >= 0) {
			struct header *h = header_new(vb.buf);
			if (! h) {
				err = -1;	// FIXME
			} else {
				err = jnl_add_action(dir, '+', h);
				header_del(h);
			}
		}
	}
	varbuf_dtor(&vb);
	int status = 200;
	if (err < 0) status = 500;
	return answer(env, seq, "PUT", status, err < 0 ? strerror(-err):"OK");
}

/*
 * CLASS
 */

int exec_class(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing CLASS in '%s'", dir);
	return answer(env, seq, "CLASS", 500, "OK");
}

/*
 * REM
 */

int exec_rem(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing REM in '%s'", dir);
	return answer(env, seq, "REM", 500, "OK");
}

