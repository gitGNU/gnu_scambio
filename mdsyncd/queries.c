/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pth.h>
#include "scambio.h"
#include "mdsyncd.h"
#include "misc.h"
#include "varbuf.h"
#include "scambio/header.h"
#include "scambio/mdir.h"
#include "scambio/cnx.h"
#include "sub.h"
#include "auth.h"

/*
 * Init
 */


void exec_begin(void)
{
}

void exec_end(void)
{
}

/*
 * Answer (protected)
 */

static void answer(struct cnx_env *env, struct mdir_cmd *cmd, int status, char const *compl)
{
	pth_mutex_acquire(&env->wfd, FALSE, NULL);
	mdir_cnx_answer(&env->cnx, cmd, status, compl);
	pth_mutex_release(&env->wfd);
}

/*
 * Subscriptions
 */

void exec_sub(struct mdir_cmd *cmd, void *user_data)
{
	struct cnx_env *const env = DOWNCAST(user_data, cnx, cnx_env);
	char const *const dir = cmd->args[0].string;
	mdir_version const version = mdir_str2version(cmd->args[1].string);
	debug("doing SUB for '%s', last version %"PRIversion, dir, version);
	int status = 200;
	// Check if we are already registered
	struct subscription *sub = subscription_find(env, dir);
	if (sub) {
		subscription_reset_version(sub, version);
		status = 304;	// signal that it's a reset
	} else {
		sub = subscription_new(env, dir, version);
		on_error status = 403;	// forbidden
	}
	answer(env, cmd, status, is_error() ? error_str():"OK");
	error_clear();	// error dealt with
}

void exec_unsub(struct mdir_cmd *cmd, void *user_data)
{
	struct cnx_env *const env = DOWNCAST(user_data, cnx, cnx_env);
	char const *const dir = cmd->args[0].string;
	debug("doing UNSUB for '%s'", dir);
	struct subscription *sub = subscription_find(env, dir);
	if (! sub) {
		answer(env, cmd, 501, "Not subscribed");
	} else {
		subscription_del(sub);
		answer(env, cmd, 200, "OK");
	}
}

/*
 * PUT/REM
 */

static void set_default_perms(struct mdir *mdir, struct mdir_user *user)
{
	struct header *perms = header_new();
	(void)header_field_new(perms, SC_TYPE_FIELD, SC_PERM_TYPE);
	(void)header_field_new(perms, SC_ALLOW_READ_FIELD,  mdir_user_name(user));
	(void)header_field_new(perms, SC_ALLOW_WRITE_FIELD, mdir_user_name(user));
	(void)header_field_new(perms, SC_ALLOW_ADMIN_FIELD, mdir_user_name(user));
	(void)header_field_new(perms, SC_DENY_READ_FIELD,  "*");
	(void)header_field_new(perms, SC_DENY_WRITE_FIELD, "*");
	(void)header_field_new(perms, SC_DENY_ADMIN_FIELD, "*");
	(void)mdir_patch(mdir, MDIR_ADD, perms, 0);
	header_unref(perms);
}

// dir is the directory user name instead of dirId, because we wan't the client
// to be able to add things to this directory before knowing it's dirId.
static mdir_version add_header(char const *dir, struct header *h, enum mdir_action action, struct mdir_user *user)
{
	mdir_version version = 0;
	debug("adding a header in dir %s", dir);
	struct mdir *mdir = mdir_lookup(dir);
	on_error return 0;

	debug("Check user permissions to write or admin here");
	if (
		(header_has_type(h, SC_PERM_TYPE) && !mdir_user_can_admin(user, mdir->permissions)) ||
		! mdir_user_can_write(user, mdir->permissions)
	) with_error(0, "No permission") return 0;

	bool is_new_dir = header_is_directory(h) && NULL == header_find(h, SC_DIRID_FIELD, NULL);
	if (is_new_dir) debug("Patch will creates a new directory");

	// Add username to the patch
	if (NULL != header_find(h, SC_USER_FIELD, NULL)) with_error(0, "Header already has username") return 0;
	(void)header_field_new(h, SC_USER_FIELD, mdir_user_name(user));

	// Patch
	if_fail (version = mdir_patch(mdir, action, h, 0)) return 0;

	// If it was a new directory, setup basic permissions for it
	if (is_new_dir) {
		char path[PATH_MAX];
		struct header_field *name_field = header_find(h, SC_NAME_FIELD, NULL);
		assert(name_field);
		snprintf(path, sizeof(path), "%s/%s", dir, name_field->value);
		debug("Add basic permissions in subdir '%s'", path);
		struct mdir *child = mdir_lookup(path);
		if_fail (set_default_perms(child, user)) return 0;
	}

	return version;
}

static void exec_putrem(enum mdir_action action, struct mdir_cmd *cmd, void *user_data)
{
	struct cnx_env *const env = DOWNCAST(user_data, cnx, cnx_env);
	char const *const dir = cmd->args[0].string;
	debug("doing %s in '%s'", cmd->def->keyword, dir);
	struct header *h;
	h = header_new();
	on_error return;
	int status = 200;
	header_read(h, env->cnx.fd);
	mdir_version version;
	on_error {
		status = 502;
	} else {
		header_debug(h);
		version = add_header(dir, h, action, env->cnx.user);
		on_error status = 502;
	}
	header_unref(h);
	answer(env, cmd, status, status == 200 ? mdir_version2str(version) : (is_error() ? error_str():"Error"));
	error_clear();
}

void exec_put(struct mdir_cmd *cmd, void *user_data)
{
	exec_putrem(MDIR_ADD, cmd, user_data);
}

void exec_rem(struct mdir_cmd *cmd, void *user_data)
{
	exec_putrem(MDIR_REM, cmd, user_data);
}

/*
 * Auth
 */

void exec_auth(struct mdir_cmd *cmd, void *user_data)
{
	struct cnx_env *const env = DOWNCAST(user_data, cnx, cnx_env);
	debug("doing AUTH");
	if_fail (env->cnx.user = mdir_user_load(cmd->args[0].string)) {
		answer(env, cmd, 500, error_str());
		error_clear();
	} else {
		answer(env, cmd, 200, "OK");
	}
}

/*
 * Quit
 */

void exec_quit(struct mdir_cmd *cmd, void *user_data)
{
	struct cnx_env *const env = DOWNCAST(user_data, cnx, cnx_env);
	debug("doing QUIT");
	env->quit = true;
	answer(env, cmd, 200, "OK");
}

