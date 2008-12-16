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
#ifndef MEREBOOK_H_081212
#define MEREBOOK_H_081212
#include <limits.h>
#include "scambio.h"
#include "scambio/header.h"
#include "scambio/mdir.h"
#include "scambio/channel.h"
#include "merelib.h"

extern struct chn_cnx ccnx;

struct contact;
struct book {
	LIST_ENTRY(book) entry;
	char path[PATH_MAX];
	char const *name;	// points into path
	struct mdir *mdir;
	LIST_HEAD(contacts, contact) contacts;
};
extern LIST_HEAD(books, book) books;

struct contact {
	LIST_ENTRY(contact) entry;
	LIST_ENTRY(contact) book_entry;
	struct book *book;
	struct header *header;
	char const *name;	// points onto header field
	mdir_version version;
};
extern struct contacts contacts;

void refresh(struct book *book);
void refresh_contact_list(void);	// refresh all the books, and renew the list displayed by the book view
GtkWidget *make_book_window(void);
GtkWidget *make_contact_window(struct contact *);

struct field_dialog {
	GtkWidget *dialog;
	GtkWidget *cat_combo, *field_combo, *value_entry;
};

struct field_dialog *field_dialog_new(GtkWindow *parent, char const *cat_name, char const *field_name, char const *value);
void field_dialog_del(struct field_dialog *fd);

#endif
