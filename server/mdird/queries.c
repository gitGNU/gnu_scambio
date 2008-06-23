#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <pth.h>
#include "mdird.h"
#include "log.h"

/*
 * Answers
 */

static int Write(int fd, void const *buf, size_t len)
{
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_write(fd, buf, len);
		if (ret < 0) {
			if (errno != EINTR) return ret;
			continue;
		}
		done += ret;
	}
	assert(done == len);
	return 0;
}

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
