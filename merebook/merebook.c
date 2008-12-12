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
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "scambio.h"
#include "merebook.h"
#include "merelib.h"
#include "auth.h"
#include "misc.h"

/*
 * Contacts
 */

struct contacts contacts = LIST_HEAD_INITIALIZER(&contacts);

static void contact_ctor(struct contact *ct, struct book *book, struct header *header, mdir_version version)
{
	debug("contact@%p, book %s, version %"PRIversion, ct, book->name, version);
	char const *name = header_search(header, SC_NAME_FIELD);
	if (!name || is_error()) return;
	ct->name = name;
	ct->header = header_ref(header);
	ct->book = book;
	ct->version = version;
	LIST_INSERT_HEAD(&book->contacts, ct, book_entry);
	LIST_INSERT_HEAD(&contacts, ct, entry);
}

struct contact *contact_new(struct book *book, struct header *header, mdir_version version)
{
	struct contact *ct = Malloc(sizeof(*ct));
	on_error return NULL;
	if_fail (contact_ctor(ct, book, header, version)) {
		free(ct);
		ct = NULL;
	}
	return ct;
}

static void contact_dtor(struct contact *ct)
{
	debug("contact@%p", ct);
	header_unref(ct->header);
	ct->header = NULL;
	LIST_REMOVE(ct, book_entry);
	LIST_REMOVE(ct, entry);
}

void contact_del(struct contact *ct)
{
	contact_dtor(ct);
	free(ct);
}

static void contact_del_by_version(mdir_version version, struct book *book)
{
	struct contact *ct, *tmp;
	LIST_FOREACH_SAFE(ct, &book->contacts, book_entry, tmp) {
		if (ct->version == version) {
			contact_del(ct);
			return;
		}
	}
}

/*
 * Books
 */

struct books books = LIST_HEAD_INITIALIZER(&books);

static void book_ctor(struct book *book, char const *path)
{
	debug("New book@%p for %s", book, path);
	int len = snprintf(book->path, sizeof(book->path), "%s", path);
	if (len >= (int)sizeof(book->path)) with_error(0, "Path too long : %s", path) return;
	book->name = book->path + len;
	while (book->name > book->path && *(book->name-1) != '/') book->name--;
	LIST_INSERT_HEAD(&books, book, entry);
	LIST_INIT(&book->contacts);
}

static struct book *book_new(char const *path)
{
	struct book *book = Malloc(sizeof(*book));
	on_error return NULL;
	if_fail (book_ctor(book, path)) {
		free(book);
		book = NULL;
	}
	return book;
}

static void book_empty(struct book *book)
{
	struct contact *ct;
	while (NULL != (ct = LIST_FIRST(&book->contacts))) {
		contact_del(ct);
	}
}

static void book_dtor(struct book *book)
{
	debug("book@%p, named %s", book, book->name);
	LIST_REMOVE(book, entry);
	book_empty(book);
}

static void book_del(struct book *book)
{
	book_dtor(book);
	free(book);
}

/*
 * Utilities
 */

static void list_contact_cb(struct mdir *mdir, struct header *header, enum mdir_action action, mdir_version version, void *data)
{
	(void)mdir;
	struct book *book = (struct book *)data;
	if (action == MDIR_REM) {
		mdir_version target = header_target(header);
		contact_del_by_version(target, book);
		return;
	}
	if (! header_has_type(header, SC_CONTACT_TYPE)) return;
	contact_new(book, header, version);
}

void refresh(struct book *book)
{
	debug("refreshing book %s", book->name);
	struct mdir *mdir = mdir_lookup(book->path);
	on_error return;
	mdir_patch_list(mdir, false, list_contact_cb, book);
}

/*
 * Main
 */

int main(int nb_args, char *args[])
{
	if_fail (init("merebook", nb_args, args)) return EXIT_FAILURE;

	static struct mdir_user *user;
	conf_set_default_str("SC_USERNAME", "Alice");
	if_fail (auth_begin()) return EXIT_FAILURE;
	atexit(auth_end);
	if_fail (user = mdir_user_load(conf_get_str("SC_USERNAME"))) return EXIT_FAILURE;
	unsigned nb_books;
	char const **book_names = header_search_all(mdir_user_header(user), "contact-dir", &nb_books);
	on_error return EXIT_FAILURE;

	while (nb_books --) {
		struct book *book;
		// TODO: these failure are interreting for the user : display an error message !
		if_fail (book = book_new(book_names[nb_books])) return EXIT_FAILURE;
		if_fail (refresh(book)) return EXIT_FAILURE;
	}
	free(book_names);
	GtkWidget *book_window = make_book_window();
	if (! book_window) return EXIT_FAILURE;
	exit_when_closed(book_window);
	gtk_widget_show_all(book_window);
	gtk_main();

	struct book *book;
	while (NULL != (book = LIST_FIRST(&books))) {
		book_del(book);
	}

	return EXIT_SUCCESS;
}

