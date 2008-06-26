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
#include "sub.h"

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

#if 0
int exec_diff(struct cnx_env *env, long long seq, char const *dir, long long version)
{
	debug("doing DIFF for '%s' from version %lld", dir, version);
	version ++;	// look for next version.
	// find where this version lies
	int err = 0;
	struct jnl *jnl;
	do {
		ssize_t offset = jnl_find(&jnl, dir, version);
		if (offset  < 0) {
			err = offset;
			break;
		}
		err = jnl_copy(jnl, offset, env->fd, &version);
		jnl_release(jnl);
	} while (! err);
	if (err == -ENOMSG) err = 0;
	return answer(env, seq, "DIFF", err < 0 ? 500:200, err < 0 ? strerror(-err):"OK");
}
#endif

/*
 * Subscriptions
 */

static struct subscription *find_subscription(struct cnx_env *env, char const *dir)
{
	struct subscription *sub;
	LIST_FOREACH(sub, &env->subscriptions, env_entry) {
		if (subscription_same_path(sub, dir)) return sub;
	}
	return NULL;
}

static int subscribe(struct cnx_env *env, char const *dir, long long version)
{
	// TODO: check permissions
	struct subscription *sub;
	int err = subscription_new(&sub, dir, version);
	if (! err) {
		LIST_INSERT_HEAD(&env->subscriptions, sub, env_entry);
		sub->env = env;
		sub->thread_id = pth_spawn(PTH_ATTR_DEFAULT, subscription_thread, sub);
	}
	return err;
}

int exec_sub(struct cnx_env *env, long long seq, char const *dir, long long version)
{
	debug("doing SUBSCR for '%s', last version %lld", dir, version);
	int err = 0;
	int substatus = 0;
	// Check if we are already registered
	struct subscription *sub = find_subscription(env, dir);
	if (sub) {
		err = subscription_reset_version(sub, version);
		substatus = 1;	// signal that it's a reset
	} else {
		err = subscribe(env, dir, version);
	}
	return answer(env, seq, "SUBSCR", (err < 0 ? 500:200)+substatus, err < 0 ? strerror(-err):"OK");
}

int exec_unsub(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing UNSUBSCR for '%s'", dir);
	struct subscription *sub = find_subscription(env, dir);
	if (! sub) return answer(env, seq, "UNSUBSCR", 501, "Not subscribed");
	(void)pth_cancel(sub->thread_id);	// better set the cancellation type to PTH_CANCEL_ASYNCHRONOUS
	LIST_REMOVE(sub, env_entry);
	subscription_del(sub);
	return answer(env, seq, "UNSUBSCR", 200, "OK");
}

/*
 * PUT/REM
 */

static int exec_putrem(char const *cmdtag, char action, struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing %s in '%s'", cmdtag, dir);
	struct varbuf vb;
	int err = varbuf_ctor(&vb, 10000, true);
	if (! err) {
		err = read_header(&vb, env->fd);
		if (err >= 0) {
			struct header *h = header_new(vb.buf);
			if (! h) {
				err = -1;	// FIXME
			} else {
				err = jnl_add_action(dir, action, h);
				header_del(h);
			}
		}
	}
	varbuf_dtor(&vb);
	return answer(env, seq, cmdtag, err < 0 ? 500:200, err < 0 ? strerror(-err):"OK");
}

int exec_put(struct cnx_env *env, long long seq, char const *dir)
{
	return exec_putrem("PUT", '+', env, seq, dir);
}

int exec_rem(struct cnx_env *env, long long seq, char const *dir)
{
	return exec_putrem("REM", '-', env, seq, dir);
}

/*
 * CLASS
 */

int exec_class(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing CLASS in '%s'", dir);
	return answer(env, seq, "CLASS", 500, "OK");
}

