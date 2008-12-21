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
#include <assert.h>
#include "scambio.h"
#include "mdirb.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "misc.h"
#include "merelib.h"

/*
 * Data Definitions
 */

/*
 * Msgs
 */

static void msg_ctor(struct msg *msg, struct mdirb *mdirb, struct header *h, mdir_version version)
{
	debug("msg@%p, version %"PRIversion, msg, version);
	msg->header = header_ref(h);
	msg->version = version;
	msg->mdirb = mdirb;
	LIST_INSERT_HEAD(&mdirb->msgs, msg, entry);
	mdirb->nb_msgs ++;
	
}

struct msg *msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct msg *msg = Malloc(sizeof(*msg));
	if_fail (msg_ctor(msg, mdirb, h, version)) {
		free(msg);
		msg = NULL;
	}
	return NULL;
}

static void msg_dtor(struct msg *msg)
{
	debug("msg@%p", msg);
	header_unref(msg->header);
	msg->header = NULL;
	LIST_REMOVE(msg, entry);
	msg->mdirb->nb_msgs --;
}

void msg_del(struct msg *msg)
{
	msg_dtor(msg);
	free(msg);
}

/*
 * MDirB
 */

static struct mdir *mdirb_alloc(void)
{
	struct mdirb *mdirb = Malloc(sizeof(*mdirb));
	LIST_INIT(&mdirb->msgs);
	mdirb->nb_msgs = 0;
	mdir_cursor_ctor(&mdirb->cursor);
	return &mdirb->mdir;
}

static void mdirb_free(struct mdir *mdir)
{
	struct mdirb *mdirb = mdir2mdirb(mdir);
	struct msg *msg;
	while (NULL != (msg = LIST_FIRST(&mdirb->msgs))) {
		msg_del(msg);
	}
	mdir_cursor_dtor(&mdirb->cursor);
	if (mdirb->window) {
		gtk_widget_destroy(mdirb->window);
		mdirb->window = NULL;
	}
	free(mdirb);
}

extern inline struct mdirb *mdir2mdirb(struct mdir *);
extern inline unsigned mdirb_size(struct mdirb *);

/*
 * Refresh an mdir msg list & count.
 * Note: Cannot do that while in mdir_alloc because the mdir is not usable yet.
 */

static void rem_msg(struct mdir *mdir, mdir_version version, void *data)
{
	(void)data;
	struct msg *msg;
	struct mdirb *mdirb = mdir2mdirb(mdir);
	debug("searching version %"PRIversion, version);
	LIST_FOREACH(msg, &mdirb->msgs, entry) {	// TODO: hash me using version please
		if (msg->version == version) {
			msg_del(msg);
			break;
		}
	}
	debug("nb_msgs in %s is now %u", mdirb->mdir.path, mdirb->nb_msgs);
}

static void add_msg(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	(void)data;
	if (header_is_directory(h)) return;
	debug("try to add msg version %"PRIversion, version);
	struct mdirb *mdirb = mdir2mdirb(mdir);
	struct msg *msg;
	if_fail (msg = msg_new(mdirb, h, version)) return;
	debug("nb_msgs in %s is now %u", mdirb->mdir.path, mdirb->nb_msgs);
}

void mdirb_refresh(struct mdirb *mdirb)
{
	debug("Refreshing mdirb %s", mdirb->mdir.path);
	mdir_patch_list(&mdirb->mdir, &mdirb->cursor, false, add_msg, rem_msg, NULL);
}

/*
 * Init
 */

void mdirb_init(void)
{
	mdir_alloc = mdirb_alloc;
	mdir_free = mdirb_free;
}

/*
 * Windowing
 */

static void unref_win(GtkWidget *widget, gpointer data)
{
	debug("unref mdirb window");
	(void)widget;
	struct mdirb *const mdirb = (struct mdirb *)data;
	if (mdirb->window) mdirb->window = NULL;
}

static void mdirb_make_window(struct mdirb *mdirb)
{
	if (mdirb->window) return;
	mdirb->window = make_window(unref_win, mdirb);
	mdirb->page = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(mdirb->window), mdirb->page);

	GtkWidget *list = gtk_vbox_new(TRUE, 0);
	struct msg *msg;
	LIST_FOREACH(msg, &mdirb->msgs, entry) {
		if (msg != LIST_FIRST(&mdirb->msgs)) {
			gtk_box_pack_start(GTK_BOX(list), gtk_hseparator_new(), FALSE, FALSE, 0);
		}
		// I would like the whole widget to be clickable but don't know how to do this
		GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
		GtkWidget *button = gtk_button_new_with_label("view");
		// TODO : connect me to the dbus msg launcher
		gtk_box_pack_start(GTK_BOX(hbox), make_msg_widget(msg->header), TRUE, TRUE, 0);
		gtk_box_pack_end  (GTK_BOX(hbox), button, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(list), hbox, FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(mdirb->page), make_scrollable(list), TRUE, TRUE, 0);

	GtkWidget *toolbar = make_toolbar(1,
		GTK_STOCK_QUIT, close_cb, mdirb->window);
	gtk_box_pack_end(GTK_BOX(mdirb->page), toolbar, FALSE, FALSE, 0);
}

void mdirb_display(struct mdirb *mdirb)
{
	if_fail (mdirb_make_window(mdirb)) return;
	gtk_widget_show_all(mdirb->window);
}

