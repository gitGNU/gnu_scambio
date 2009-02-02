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
#include "misc.h"
#include "merelib.h"
#include "vadrouille.h"
#include "dialog.h"
#include "perm.h"

/*
 * Permission messages are just plain sc_msg
 */

static struct sc_plugin perm_plugin;

static struct sc_msg *perm_msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct sc_msg *msg = Malloc(sizeof(*msg));
	if_fail (sc_msg_ctor(msg, mdirb, h, version, &perm_plugin)) {
		free(msg);
		return NULL;
	}
	return msg;
}

static void perm_msg_del(struct sc_msg *msg)
{
	sc_msg_dtor(msg);
	free(msg);
}

/*
 * Directory Functions
 */

struct perm_editor {
	struct sc_view view;
	GtkWidget *vbox;
	struct mdirb *mdirb;
	unsigned nb_rows;
	struct pe_row {
		GtkWidget *hbox, *add_button, *del_button;
		GtkWidget *combo, *entry;
	} rows[256];
};

static struct type2select {
	char const *field, *label;
} const type2select[] = {
	{ SC_ALLOW_WRITE_FIELD, "allow write" },
	{ SC_ALLOW_READ_FIELD,  "allow read" },
	{ SC_ALLOW_ADMIN_FIELD, "allow admin" },
	{ SC_DENY_WRITE_FIELD,  "deny write" },
	{ SC_DENY_READ_FIELD,   "deny read" },
	{ SC_DENY_ADMIN_FIELD,  "deny admin" },
};

static void perm_editor_dtor(struct perm_editor *editor)
{
	sc_view_dtor(&editor->view);
}

static void perm_editor_del(struct sc_view *view)
{
	struct perm_editor *editor = DOWNCAST(view, view, perm_editor);
	perm_editor_dtor(editor);
	free(editor);
}

static void editor_add_row(struct perm_editor *, struct header_field *, unsigned);

static void editor_insert_row_after(struct perm_editor *editor, unsigned t)
{
	// Scroll down rows
	unsigned nb_rows = editor->nb_rows;
	assert(t < nb_rows);
	if (nb_rows >= sizeof_array(editor->rows)) {
		alert(GTK_MESSAGE_ERROR, "Too many rules already");
		return;
	}
	memmove(editor->rows + t +2, editor->rows + t +1, sizeof(*editor->rows) * (nb_rows - t -1));

	// Trick editor_add_row()
	editor->nb_rows = t+1;
	editor_add_row(editor, NULL, 0);
	editor->nb_rows = nb_rows +1;
}

static unsigned get_row_from_button(struct perm_editor *editor, GtkWidget *button, size_t offset)
{
	for (unsigned t = 0; t < editor->nb_rows; t++) {
		struct pe_row *const row = editor->rows + t;
		GtkWidget *row_button = *(GtkWidget **)((char *)row + offset);
		if (row_button == button) return t;
	}
	assert(0);
	return 0;
}

static void add_row_cb(GtkWidget *button, gpointer data)
{
	struct perm_editor *editor = (struct perm_editor *)data;
	unsigned t = get_row_from_button(editor, button, offsetof(struct pe_row, add_button));

	int type = gtk_combo_box_get_active(GTK_COMBO_BOX(editor->rows[t].combo));
	char const *username = gtk_entry_get_text(GTK_ENTRY(editor->rows[t].entry));
	if (type < 0 || type >= (int)sizeof_array(type2select) || strlen(username) == 0) {
		alert(GTK_MESSAGE_ERROR, "You must choose a permission type and a username");
		return;
	}

	// We merely add a new blank entry
	editor_insert_row_after(editor, t);
}

static void del_row_cb(GtkWidget *button, gpointer data)
{
	struct perm_editor *editor = (struct perm_editor *)data;
	unsigned t = get_row_from_button(editor, button, offsetof(struct pe_row, del_button));

	// Do not delete the last one
	if (editor->nb_rows <= 1) {
		alert(GTK_MESSAGE_ERROR, "Nonsense !");
		return;
	}

	gtk_widget_destroy(editor->rows[t].hbox);
	// Scroll up rows
	editor->nb_rows --;
	memmove(editor->rows + t, editor->rows + t+1, sizeof(*editor->rows) * (editor->nb_rows - t));
}

static void editor_add_row(struct perm_editor *editor, struct header_field *hf, unsigned selected)
{
	struct pe_row *row = editor->rows + (editor->nb_rows++);
	
	// A combo box with possible perm fields
	row->combo = gtk_combo_box_new_text();
	for (unsigned t = 0; t < sizeof_array(type2select); t++) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(row->combo), type2select[t].label);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(row->combo), selected);
	
	// An entry for user name
	row->entry = gtk_entry_new();
	if (hf) {
		gtk_entry_set_text(GTK_ENTRY(row->entry), hf->value);
	}

	row->hbox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row->hbox), row->combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row->hbox), row->entry, TRUE, TRUE, 0);

	// Some buttons
	row->add_button = gtk_button_new_from_stock(GTK_STOCK_ADD);
	g_signal_connect(row->add_button, "clicked", G_CALLBACK(add_row_cb), editor);
	gtk_box_pack_end(GTK_BOX(row->hbox), row->add_button, FALSE, FALSE, 0);
	row->del_button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	g_signal_connect(row->del_button, "clicked", G_CALLBACK(del_row_cb), editor);
	gtk_box_pack_end(GTK_BOX(row->hbox), row->del_button, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(editor->vbox), row->hbox, FALSE, FALSE, 0);
	gtk_widget_show_all(row->hbox);
}

static void apply_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	assert(! is_error());
	struct perm_editor *editor = (struct perm_editor *)user_data;

	// Build a header from the widget values
	struct header *h = header_new();
	(void)header_field_new(h, SC_TYPE_FIELD, SC_PERM_TYPE);
	for (unsigned r = 0; r < editor->nb_rows; r++) {
		unsigned field = gtk_combo_box_get_active(GTK_COMBO_BOX(editor->rows[r].combo));
		assert(field < sizeof_array(type2select));
		char const *username = gtk_entry_get_text(GTK_ENTRY(editor->rows[r].entry));
		if (! username) {
			alert(GTK_MESSAGE_ERROR, "Username field must not be blank (use '*' for all)");
			header_unref(h);
			return;
		}
		(void)header_field_new(h, type2select[field].field, username);
	}

	// Write it
	if_fail (mdir_patch_request(&editor->mdirb->mdir, MDIR_ADD, h)) {
		alert_error();
	} else {
		mdirb_refresh(editor->mdirb);
		gtk_widget_destroy(editor->view.window);
	}
	header_unref(h);
}

static struct sc_view *perm_editor_new(struct header *header, char const *name, struct mdirb *mdirb)
{
	struct perm_editor *editor = Malloc(sizeof(*editor));
	editor->nb_rows = 0;
	editor->mdirb = mdirb;
	editor->vbox = gtk_vbox_new(TRUE, 0);
	GtkWidget *global_vbox = gtk_vbox_new(FALSE, 0);

	// A title
	// TODO: Check that user_can_admin this directory, and if not display a warning
	GtkWidget *title = gtk_label_new(NULL);
	char *markup = g_markup_printf_escaped("Edit permissions for folder <span style=\"italic\">%s</span>", name);
	gtk_label_set_markup(GTK_LABEL(title), markup);
	g_free(markup);
	gtk_box_pack_start(GTK_BOX(global_vbox), title, FALSE, FALSE, 0);

	struct header_field *hf = NULL;
	while (NULL != (hf = header_find(header, NULL, hf))) {
		for (
			unsigned t = 0;
			t < sizeof_array(type2select) && editor->nb_rows < sizeof_array(editor->rows);
			t++
		) {
			if (0 != strcmp(hf->name, type2select[t].field)) continue;
			editor_add_row(editor, hf, t);
			break;
		}
	}

	GtkWidget *window = make_window(WC_EDITOR, NULL, NULL);
	gtk_box_pack_start(GTK_BOX(global_vbox), make_scrollable(editor->vbox), TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(window), global_vbox);
	
	GtkWidget *toolbar = make_toolbar(2,
		GTK_STOCK_APPLY,  apply_cb, editor,
		GTK_STOCK_CANCEL, close_cb, window);
	gtk_box_pack_end(GTK_BOX(global_vbox), toolbar, FALSE, FALSE, 0);

	sc_view_ctor(&editor->view, perm_editor_del, window);
	return &editor->view;
}

static void set_perms(struct mdirb *mdirb, char const *name, GtkWindow *parent)
{
	(void)parent;
	(void)name;
	struct header *header;
	if (mdirb->mdir.permissions) {
		debug("Editing known permissions");
		header = header_ref(mdirb->mdir.permissions);
	} else {
		debug("Editing unknown permissions");
		header = header_new();
		(void)header_field_new(header, SC_TYPE_FIELD, SC_PERM_TYPE);
	}
	(void)perm_editor_new(header, name, mdirb);
	on_error alert_error();
	header_unref(header);
}

/*
 * View a Permission
 */

static struct sc_msg_view *perm_view_new(struct sc_msg *msg)
{
	(void)msg;	// TODO
	return NULL;
}

/*
 * Directory Functions
 */

// Later

/*
 * Message Description
 */

static char *perm_msg_descr(struct sc_msg *msg)
{
	(void)msg;	// TODO
	return g_markup_printf_escaped("New permissions");
}

static char *perm_msg_icon(struct sc_msg *msg)
{
	(void)msg;
	return GTK_STOCK_NEW;
}

/*
 * Init
 */

static struct sc_plugin_ops const ops = {
	.msg_new          = perm_msg_new,
	.msg_del          = perm_msg_del,
	.msg_descr        = perm_msg_descr,
	.msg_icon         = perm_msg_icon,
	.msg_view_new     = perm_view_new,
	.msg_view_del     = NULL,
	.dir_view_new     = NULL,
	.dir_view_del     = NULL,
	.dir_view_refresh = NULL,
};
static struct sc_plugin perm_plugin = {
	.name = "permission",
	.type = SC_PERM_TYPE,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 1,
	.dir_functions = {
		{ TOSTR(ICONDIR)"/26x26/apps/perm.png", NULL, set_perms },
	},
};

void perm_init(void)
{
	sc_plugin_register(&perm_plugin);
}
