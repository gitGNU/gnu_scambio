#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
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

/*
 * (De)Init
 */

static unsigned type_key, dirid_key, name_key;
struct persist dirid_seq;

int exec_begin(void)
{
	type_key  = header_key("type");
	dirid_key = header_key("dirId");
	name_key  = header_key("name");
	conf_set_default_str("MDIRD_DIRSEQ", "/tmp/.dirid.seq");
	return persist_ctor(&dirid_seq, sizeof(long long), conf_get_str("MDIRD_DIRSEQ"));
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
	size_t len = snprintf(reply, sizeof(reply), "%lld %s %d %s\n", seq, cmd_name, status, cmpl);
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

static bool is_directory(struct header *h) {
	char const *type = header_search(h, "type", type_key);
	return type && 0 == strncmp("dir", type, 3);
}

// Reads fd until a "::" line, then build a header object
static int read_header(struct header **hp, struct varbuf *vb, int fd)
{
	debug("read_header(%p, %p, %d)", hp, vb, fd);
	int err = 0;
	int nb_lines = 0;
	char *line;
	bool eoh_reached = false;
	while (0 == (err = varbuf_read_line(vb, fd, MAX_HEADLINE_LENGTH, &line))) {
		if (++ nb_lines > MAX_HEADER_LINES) {
			err = -E2BIG;
			break;
		}
		if (line_match(line, "::")) {
			// forget this line
			vb->used = line - vb->buf + 1;
			vb->buf[vb->used-1] = '\0';
			eoh_reached = true;
			break;
		}
	}
	if (nb_lines == 0) err = -EINVAL;
	if (! eoh_reached) err = -EINVAL;
	if (err == 1) err = 0;	// no more use for EOF
	if (! err) {
		*hp = header_new(vb->buf);
		if (! *hp) err = -1;	// FIXME
	}
	return err;
}

// dir is the directory user name instead of dirId, because we wan't the client
// to be able to add things to this directory before knowing it's dirId.
static int add_header(char const *dir, struct header *h, char action)
{
	int err = 0;
	if (is_directory(h)) {
		char const *dirid_str = header_search(h, "dirId", dirid_key);
		char const *dirname   = header_search(h, "name", name_key);
		long long dirid;
		debug("header is for directory id=%s, name=%s", dirid_str, dirname);
		if (action == '+') {
			if (! dirname) dirname = "Unnamed";
			if (dirid_str) err = -EINVAL;
			if (! err) {
				char dirid_add[20+1];	// storage for additional temporary header field
				dirid = (*(long long *)dirid_seq.data)++;
				snprintf(dirid_add, sizeof(dirid_add), "%lld", dirid);
				err = header_add_field(h, "dirId", dirid_key, dirid_add);
				if (! err) {
					err = jnl_createdir(dir, dirid, dirname);
					(void)header_chop_field(h, dirid_key);
				}
			}
		} else {
			if (! dirname) err = -EINVAL;
			if (! err) {
				dirid = strtoll(dirid_str, NULL, 0);
				err = jnl_unlinkdir(dir, dirname);
			}
		}
	}
	if (! err) err = jnl_add_patch(dir, action, h);
	return err;
}

static int exec_putrem(char const *cmdtag, char action, struct cnx_env *env, long long seq, char const *dir)
{
	debug("doing %s in '%s'", cmdtag, dir);
	int err = 0;
	struct header *h;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 10000, true))) return err;
	err = read_header(&h, &vb, env->fd);
	if (! err) {
		err = add_header(dir, h, action);
		header_del(h);
	}
	varbuf_dtor(&vb);
	return answer(env, seq, cmdtag, err ? 500:200, err ? strerror(-err):"OK");
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
		if (*dir != '/' && *dir != '.') return false;
		dir++;
	}
	return true;
}

static int create_if_missing(char const *dir, char const *subdir)
{
#	define DIRFIELD "type: dir\nname: %s\n"
#	define DIRFIELD_LEN (17 + PATH_MAX)
	char str[DIRFIELD_LEN];
	COMPILE_ASSERT(DIRFIELD_LEN > PATH_MAX);	// we are going to use str as a temporary path
	snprintf(str, sizeof(str), "%s/%s", dir, subdir);
	int err = dir_exist(str);
	if (err > 0) return 0;
	if (err < 0) return err;
	snprintf(str, sizeof(str), DIRFIELD, subdir);
	struct header *h = header_new(str);
	if (! h) return -1;	// FIXME
	err = add_header(dir, h, '+');
	header_del(h);
	return err;
}

static int class_rec(struct cnx_env *env, long long seq, char const *dir, struct header *h);
static int goto_dir(struct cnx_env *env, long long seq, char const *dir, struct strib_action const *action, struct header *h)
{
	int err = 0;
	char const *dest;
	switch (action->dest_type) {
		case DEST_DEREF:
			dest = header_search(h, action->dest.deref.name, action->dest.deref.key);
			if (! dest) {
				error("dereferencing unset field '%s'", action->dest.deref.name);
				return -ENOENT;
			}
			break;
		case DEST_STRING:
			dest = action->dest.string;
			break;
	}
	debug("Delivering to %s", dest);
	if (void_dir(dest)) return -EINVAL;	// avoid staying here
	if (0 != (err = create_if_missing(dir, dest))) return err;
	char *new_dir = malloc(PATH_MAX);	// this is ercursive : don't allow malicious users to exhaust our stack
	if (! new_dir) return -ENOMEM;
	snprintf(new_dir, PATH_MAX, "%s/%s", dir, dest);
	err = class_rec(env, seq, new_dir, h);
	free(new_dir);
	return err;
}

static int class_rec(struct cnx_env *env, long long seq, char const *dir, struct header *h)
{
	struct stribution *strib;
	int err = strib_get(&strib, dir);
	if (err) return err;
	if (! strib) {	// just stores it there
		return add_header(dir, h, '+');
	}
	struct strib_action const **actions = malloc(NB_MAX_TESTS * sizeof(*actions));
	if (! actions) return -ENOMEM;
	unsigned nb_actions = strib_eval(strib, h, actions);
	struct strib_action const **tmp = realloc(actions, nb_actions * sizeof(*actions));
	if (tmp) actions = tmp;
	debug("Evaluate to %u actions", nb_actions);
	for (unsigned a=0; !err && a<nb_actions; a++) {
		switch (actions[a]->type) {
			case ACTION_DISCARD: err = 0; goto bigbreak;
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
	int err = 0;
	struct header *h;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 10000, true))) goto answ;
	if (0 != (err = read_header(&h, &vb, env->fd))) goto q1;
	if (is_directory(h)) {
		err = -EISDIR;
		goto q2;
	}
	err = class_rec(env, seq, dir, h);
q2:
	header_del(h);
q1:
	varbuf_dtor(&vb);
answ:
	return answer(env, seq, "CLASS", err ? 500:200, err ? strerror(-err):"OK");
}

/*
 * Quit
 */

int exec_quit (struct cnx_env *env, long long seq)
{
	debug("doing QUIT");
	return answer(env, seq, "QUIT", 200, "OK");
}
