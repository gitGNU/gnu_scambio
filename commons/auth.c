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
#include "scambio/header.h"

/*
 * Data Definitions
 */

struct mdir_user {
	LIST_ENTRY(mdir_user) entry;
	char *name;
	struct header *header;
};

static LIST_HEAD(users, mdir_user) users;
static char const *users_root;

/*
 * User
 */

static void user_ctor(struct mdir_user *usr, char const *name, struct header *header)
{
	usr->name = strdup(name);
	if (! usr->name) with_error(errno, "strdup(%s)", name) return;
	usr->header = header;
	LIST_INSERT_HEAD(&users, usr, entry);
}

static void user_dtor(struct mdir_user *usr)
{
	LIST_REMOVE(usr, entry);
	free(usr->name);
	header_del(usr->header);
}

static struct mdir_user *user_new(char const *name, struct header *header)
{
	struct mdir_user *usr = malloc(sizeof(*usr));
	if (! usr) with_error(ENOMEM, "malloc user") return NULL;
	if_fail (user_ctor(usr, name, header)) {
		free(usr);
		usr = NULL;
	}
	return usr;
}

static void user_del(struct mdir_user *usr)
{
	user_dtor(usr);
	free(usr);
}

static struct mdir_user *user_refresh(struct mdir_user *usr)
{
	// TODO: if file changed reload
	return usr;
}

struct mdir_user *mdir_user_load(char const *name)
{
	struct mdir_user *usr;
	LIST_FOREACH(usr, &users, entry) {
		if (0 == strcmp(name, usr->name)) return user_refresh(usr);
	}
	char filename[PATH_MAX];
	snprintf(filename, sizeof(filename), "%s/%s", users_root, name);
	struct header *header = header_from_file(filename);
	on_error return NULL;
	usr = user_new(name, header);
	on_error {
		header_del(header);
		return NULL;
	}
	return usr;
}

/*
 * Initialization
 */

void auth_begin(void)
{
	if_fail (conf_set_default_str("MDIR_USERS_DIR", "/var/lib/scambio/users")) return;
	users_root = conf_get_str("MDIR_USERS_DIR");
	LIST_INIT(&users);
}

void auth_end(void)
{
	struct mdir_user *usr;
	while (NULL != (usr = LIST_FIRST(&users))) {
		user_del(usr);
	}
}

