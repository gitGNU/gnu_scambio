#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <pth.h>
#include "mdird.h"
#include "log.h"
#include "misc.h"

/*
 * Misc
 */

static int read_header(struct varbuf *vb, int fd)
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

/*
 * Answers
 */

static int answer(struct cnx_env *env, long long seq, char const *cmd_name, int status)
{
	char reply[100];
	size_t len = snprintf(reply, sizeof(reply), "%lld %s %d\n", seq, cmd_name, status);
	assert(len < sizeof(reply));
	return Write(env->fd, reply, len);
}

/*
 * DIFF
 */

int exec_diff(struct cnx_env *env, long long seq, char const *dir, long long version)
{
	debug("doing DIFF for '%s' from version %lld", dir, version);
	return answer(env, seq, "DIFF", 500);
}

/*
 * PUT
 */

int exec_put(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing PUT in '%s'", dir);
	return answer(env, seq, "PUT", 500);
}

/*
 * CLASS
 */

int exec_class(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing CLASS in '%s'", dir);
	return answer(env, seq, "CLASS", 500);
}

/*
 * REM
 */

int exec_rem(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing REM in '%s'", dir);
	return answer(env, seq, "REM", 500);
}
