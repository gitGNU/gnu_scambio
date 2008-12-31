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
#ifndef CONTACT_H_081226
#define CONTACT_H_081226

struct contact {
	struct sc_msg msg;
	char const *name;	// points onto header field
	LIST_ENTRY(contact) entry;	// All contacts are chained together
};

void contact_init(void);

struct field_dialog {
	GtkWidget *dialog;
	GtkWidget *cat_combo, *field_combo, *value_entry;
};

struct field_dialog *field_dialog_new(GtkWindow *parent, char const *cat_name, char const *field_name, char const *value);
void field_dialog_del(struct field_dialog *fd);

/*
 * Contact Picker
 */

struct contact_picker;
typedef void contact_picker_cb(struct contact_picker *, struct contact *);

struct contact_picker {
	GtkWidget *button;
	contact_picker_cb *cb;
	GtkWindow *parent;
};

void contact_picker_ctor(struct contact_picker *picker, contact_picker_cb *cb, GtkWindow *parent);
void contact_picker_dtor(struct contact_picker *picker);

#endif
