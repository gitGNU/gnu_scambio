#include <string.h>
#include <errno.h>
#include "scambio.h"
#include "auth.h"
#include "scambio/header.h"

/*
 * Data Definitions
 */

struct user {
	LIST_ENTRY(user) entry;
	char *name;
	struct header *header;
};

static LIST_HEAD(users, user) users;
static char const *users_root;

/*
 * User
 */

static void user_ctor(struct user *usr, char const *name, struct header *header)
{
	usr->name = strdup(name);
	if (! usr->name) with_error(errno, "strdup(%s)", name) return;
	usr->header = header;
	LIST_INSERT_HEAD(&users, usr, entry);
}

static void user_dtor(struct user *usr)
{
	LIST_REMOVE(usr, entry);
	free(usr->name);
	header_del(usr->header);
}

static struct user *user_new(char const *name, struct header *header)
{
	struct user *usr = malloc(sizeof(*usr));
	if (! usr) with_error(ENOMEM, "malloc user") return NULL;
	if_fail (user_ctor(usr, name, header)) {
		free(usr);
		usr = NULL;
	}
	return usr;
}

static void user_del(struct user *usr)
{
	user_dtor(usr);
	free(usr);
}

static struct user *user_refresh(struct user *usr)
{
	// TODO: if file changed reload
	return usr;
}

struct user *user_load(char const *name)
{
	struct user *usr;
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
	if_fail (conf_set_default_str("MDIR_USERS_DIR", "/tmp/mdir/users")) return;
	users_root = conf_get_str("MDIR_USERS_DIR");
	LIST_INIT(&users);
}

void auth_end(void)
{
	struct user *usr;
	while (NULL != (usr = LIST_FIRST(&users))) {
		user_del(usr);
	}
}

