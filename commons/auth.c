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
#include <string.h>
#include <errno.h>
#include "scambio.h"
#include "auth.h"
#include "misc.h"
#include "scambio/header.h"

/*
 * Data Definitions
 */

struct mdir_user {
	LIST_ENTRY(mdir_user) entry;
	char *name;	// points onto header
	struct header *header;	// must have a name, and may have 'alias' fields for grouping users
	unsigned tag;	// used to tag the user in a alias tree traversal
};

static LIST_HEAD(users, mdir_user) users;
static char const *users_root;
static struct header *default_perms;

/*
 * User
 */

static void user_ctor(struct mdir_user *user, char const *name, struct header *header)
{
	user->name = strdup(name);
	if (! user->name) with_error(errno, "strdup(%s)", name) return;
	user->header = header;
	user->tag = 0;
	LIST_INSERT_HEAD(&users, user, entry);
}

static void user_dtor(struct mdir_user *user)
{
	LIST_REMOVE(user, entry);
	free(user->name);
	header_unref(user->header);
}

static struct mdir_user *user_new(char const *name, struct header *header)
{
	struct mdir_user *user = malloc(sizeof(*user));
	if (! user) with_error(ENOMEM, "malloc user") return NULL;
	if_fail (user_ctor(user, name, header)) {
		free(user);
		user = NULL;
	}
	return user;
}

static void user_del(struct mdir_user *user)
{
	user_dtor(user);
	free(user);
}

static struct mdir_user *user_refresh(struct mdir_user *user)
{
	// TODO: if file changed reload
	return user;
}

struct mdir_user *mdir_user_load(char const *name)
{
	debug("name=%s", name);
	struct mdir_user *user;
	LIST_FOREACH(user, &users, entry) {
		if (0 == strcmp(name, user->name)) return user_refresh(user);
	}
	char filename[PATH_MAX];
	snprintf(filename, sizeof(filename), "%s/%s", users_root, name);
	struct header *header = header_from_file(filename);
	on_error return NULL;
	user = user_new(name, header);
	on_error {
		header_unref(header);
		return NULL;
	}
	return user;
}

struct header *mdir_user_header(struct mdir_user *user)
{
	return user->header;
}

char const *mdir_user_name(struct mdir_user *user)
{
	return user->name;
}

static bool is_in_group_rec(struct mdir_user *user, char const *groupname, unsigned tag)
{
	if (0 == strcmp("*", groupname)) return true;	// "*" is an alias for all
	struct mdir_user *group = mdir_user_load(groupname);
	on_error return false;
	if (user == group) return true;
	if (group->tag == tag) return false;
	group->tag = tag;
	struct header_field *hf = NULL;
	while (NULL != (hf = header_find(group->header, SC_ALIAS_FIELD, hf))) {
		bool res = is_in_group_rec(user, hf->value, tag);
		on_error return false;
		if (res) return true;
	}
	return false;
}

bool mdir_user_is_in_group(struct mdir_user *user, char const *groupname, bool if_unknown)
{
	static unsigned tag = 0;
	bool res = is_in_group_rec(user, groupname, ++tag);
	on_error {
		error_clear();
		return if_unknown;
	}
	return res;
}

static bool check_user_perms(struct mdir_user *user, struct header *header, char const *allow_field, char const *deny_field)
{
	if (header) {
		struct header_field *hf = NULL;
		TAILQ_FOREACH(hf, &header->fields, entry) {
			if (0 == strcmp(hf->name, allow_field)) {
				// We suppose unknown users are not us
				if (mdir_user_is_in_group(user, hf->value, false)) return true;
			} else if (0 == strcmp(hf->name, deny_field)) {
				// We suppose we are in this unknown group
				if (mdir_user_is_in_group(user, hf->value, true)) return false;
			}
		}
		// If we can't find out user perms, use default one
	}
	
	if (! default_perms || header == default_perms) return false;	// When everything fails, deny all perms

	return check_user_perms(user, default_perms, allow_field, deny_field);
}

bool mdir_user_can_admin(struct mdir_user *user, struct header *header)
{
	return check_user_perms(user, header, SC_ALLOW_ADMIN_FIELD, SC_DENY_ADMIN_FIELD);
}

bool mdir_user_can_read(struct mdir_user *user, struct header *header)
{
	return
		check_user_perms(user, header, SC_ALLOW_READ_FIELD, SC_DENY_READ_FIELD) ||
		mdir_user_can_admin(user, header);
}

bool mdir_user_can_write(struct mdir_user *user, struct header *header)
{
	return
		check_user_perms(user, header, SC_ALLOW_WRITE_FIELD, SC_DENY_WRITE_FIELD) ||
		mdir_user_can_admin(user, header);
}

/*
 * Initialization
 */

static void auth_deinit(void)
{
	struct mdir_user *user;
	while (NULL != (user = LIST_FIRST(&users))) {
		user_del(user);
	}
	if (default_perms) {
		header_unref(default_perms);
		default_perms = NULL;
	}
}

void auth_init(void)
{
	if_fail (conf_set_default_str("SC_USERNAME", "Alice")) return;
	if_fail (conf_set_default_str("SC_MDIR_USERS_DIR", "/var/lib/scambio/users")) return;
	users_root = conf_get_str("SC_MDIR_USERS_DIR");
	LIST_INIT(&users);
	Mkdir(users_root);
	default_perms = header_new();
	header_field_new(default_perms, SC_ALLOW_READ_FIELD, "*");
	header_field_new(default_perms, SC_DENY_WRITE_FIELD, "*");
	header_field_new(default_perms, SC_ALLOW_ADMIN_FIELD, "admin");
	header_field_new(default_perms, SC_DENY_ADMIN_FIELD, "*");
	atexit(auth_deinit);
}

