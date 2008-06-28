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
#include "stribution.h"
#include "persist.h"
#include "conf.h"

/*
 * (De)Init
 */

static unsigned type_key, dirid_key;
struct persist dirid_seq;

int exec_begin(void)
{
	type_key = header_key("type");
	dirid_key = header_key("dirId");
	conf_set_default_str("SCAMBIO_DIRSEQ", "/tmp/dirid.seq");
	return persist_ctor(&dirid_seq, sizeof(long long), conf_get_str("SCAMBIO_DIRSEQ"));
}

void exec_end(void) {
	persist_dtor(&dirid_seq);
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
 * Subscriptions
 */

int exec_sub(struct cnx_env *env, long long seq, char const *dir, long long version)
{
	debug("doing SUB for '%s', last version %lld", dir, version);
	int err = 0;
	int substatus = 0;
	// Check if we are already registered
	struct subscription *sub = subscription_find(env, dir);
	if (sub) {
		subscription_reset_version(sub, version);
		substatus = 1;	// signal that it's a reset
	} else {
		err = subscription_new(&sub, env, dir, version);
	}
	return answer(env, seq, "SUB", (err < 0 ? 500:200)+substatus, err < 0 ? strerror(-err):"OK");
}

int exec_unsub(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing UNSUB for '%s'", dir);
	struct subscription *sub = subscription_find(env, dir);
	if (! sub) return answer(env, seq, "UNSUB", 501, "Not subscribed");
	(void)pth_cancel(sub->thread_id);	// better set the cancellation type to PTH_CANCEL_ASYNCHRONOUS
	LIST_REMOVE(sub, env_entry);
	subscription_del(sub);
	return answer(env, seq, "UNSUB", 200, "OK");
}

/*
 * PUT/REM
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

static bool is_directory(struct header *h) {
	char const *type = header_search(h, "type", type_key);
	return type && 0 == strncmp("dir", type, 3);
}

// Reads fd until a "%%" line, then build a header object
static int read_header(struct header **hp, int fd)
{
	int err = 0;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 10000, true))) return err;
	int nb_lines = 0;
	char *line;
	bool eoh_reached = false;
	while (0 < (err = varbuf_read_line(&vb, fd, MAX_HEADLINE_LENGTH, &line))) {
		if (++ nb_lines > MAX_HEADER_LINES) {
			err = -E2BIG;
			break;
		}
		if (line_match(line, "%%")) {
			// forget this line
			vb.used = line - vb.buf + 1;
			vb.buf[vb.used-1] = '\0';
			eoh_reached = true;
			break;
		}
	}
	if (nb_lines == 0) err = -EINVAL;
	if (! eoh_reached) err = -EINVAL;
	if (! err) {
		*hp = header_new(vb.buf);
		if (! *hp) err = -1;	// FIXME
	}
	varbuf_dtor(&vb);
	return err;
}

static int exec_putrem(char const *cmdtag, char action, struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing %s in '%s'", cmdtag, dir);
	struct header *h;
	char dirid_add[20+1];	// storage for additional header field on the stack, OK since the header is itself on this frame
	int err = read_header(&h, env->fd);
	if (! err) {
		if (is_directory(h)) {
			char const *dirid_str = header_search(h, "dirId", dirid_key);
			long long dirid;
			if (dirid_str) {
				dirid = strtoll(dirid_str, NULL, 0);
			} else {
				dirid = (*(long long *)dirid_seq.data)++;
				snprintf(dirid_add, sizeof(dirid_add), "%lld", dirid);
				err = header_add_field(h, "dirId", dirid_key, dirid_add);
			}
			if (! err) {
				if (action == '+') {
					err = jnl_createdir(dir, dirid);
				} else {
					dirid = strtoll(dirid_str, NULL, 10);
					err = jnl_unlinkdir(dir, dirid);
				}
			}
		}
		if (! err) err = jnl_add_patch(dir, action, h);
		header_del(h);
	}
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

static bool void_dir(char const *dir)
{
	while (*dir != '\0') {
		if (*dir != '/' && *dir != '.') return true;
		dir++;
	}
	return false;
}

static int class_rec(struct cnx_env *env, long long seq, char const *dir, struct header *h);
static int goto_dir(struct cnx_env *env, long long seq, char const *dir, struct strib_action const *action, struct header *h)
{
	if (action->dest_type != DEST_STRING) {
		error("field target actions not implemented yet");
		return -EINVAL;
	}
	if (void_dir(action->dest.string)) return -EINVAL;	// avoid staying here
	char *new_dir = malloc(PATH_MAX);	// this is ercursive : don't allow malicious users to exhaust our stack
	if (! new_dir) return -ENOMEM;
	snprintf(new_dir, PATH_MAX, "%s/%s", dir, action->dest.string);
	int err = class_rec(env, seq, dir, h);
	free(new_dir);
	return err;
}

static int class_rec(struct cnx_env *env, long long seq, char const *dir, struct header *h)
{
	struct stribution *strib;
	int err = strib_get(&strib, dir);
	if (err) return err;
	if (! strib) {	// just stores it there
		return exec_putrem("CLASS", '+', env, seq, dir);
	}
	struct strib_action const **actions = malloc(NB_MAX_TESTS * sizeof(*actions));
	if (! actions) return -ENOMEM;
	unsigned nb_actions = strib_eval(strib, h, actions);
	struct strib_action const **tmp = realloc(actions, nb_actions * sizeof(*actions));
	if (tmp) actions = tmp;
	for (unsigned a=0; !err && a<nb_actions; a++) {
		switch (actions[a]->type) {
			case ACTION_DISCARD: err = 0; break;
			case ACTION_COPY:    err = goto_dir(env, seq, dir, actions[a], h); break;
			case ACTION_MOVE:    err = goto_dir(env, seq, dir, actions[a], h); goto bigbreak;
		}
	}
bigbreak:
	free(actions);
	return err;
}

int exec_class(struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing CLASS in '%s'", dir);
	struct header *h;
	int err = read_header(&h, env->fd);
	if (is_directory(h)) return -EISDIR;
	if (! err) err = class_rec(env, seq, dir, h);
	return answer(env, seq, "CLASS", 500, "OK");
}

/*
 * Quit
 */

int exec_quit (struct cnx_env *env, long long seq)
{
	debug("doing QUIT");
	return answer(env, seq, "QUIT", 200, "OK");
}
