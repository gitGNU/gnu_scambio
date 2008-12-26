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
#ifndef VADROUILLE_H_081222
#define VADROUILLE_H_081222

#include <assert.h>
#include "scambio/channel.h"

struct chn_cnx ccnx;

/* API for plugins.
 */

#include "mdirb.h"

struct sc_view;
struct sc_msg_view;
struct sc_dir_view;

struct sc_plugin {
	LIST_ENTRY(sc_plugin) entry;
	char const *name;
	char const *type;
	struct sc_plugin_ops {
		struct sc_msg *(*msg_new)(struct mdirb *, struct header *, mdir_version);
		void (*msg_del)(struct sc_msg *);
		char *(*msg_descr)(struct sc_msg *msg);
		struct sc_msg_view *(*msg_view_new)(struct sc_msg *);
		void (*msg_view_del)(struct sc_view *);
		struct sc_dir_view *(*dir_view_new)(struct mdirb *);
		void (*dir_view_del)(struct sc_view *);
	} const *ops;
	unsigned nb_global_functions;
	struct sc_plugin_global_function {
		GtkWidget *icon;	// may be NULL
		char const *label;
		void (*cb)(void);	// may be NULL
	} global_functions[8];
	unsigned nb_dir_functions;
	struct sc_plugin_dir_function {
		GtkWidget *icon;
		char const *label;
		void (*cb)(struct mdirb *);
	} dir_functions[8];
};

struct sc_view {
	GtkWidget *window;
	void (*del)(struct sc_view *);
};

void unref_view(GtkWidget *widget, gpointer data);
static inline void sc_view_ctor(struct sc_view *view, void (*del)(struct sc_view *), GtkWidget *window)
{
	view->window = window;
	view->del = del;
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(unref_view), view);
	gtk_widget_show_all(window);
}

static inline void sc_view_dtor(struct sc_view *view)
{
	debug("view@%p, window = %p", view, view->window);
	if (view->window) {
		gtk_widget_destroy(view->window);
		assert(view->window == NULL);
	}
}

#include "msg.h"

struct sc_msg_view {
	struct sc_view view;
	struct sc_msg *msg;
};

static inline struct sc_msg_view *view2msg_view(struct sc_view *view)
{
	return DOWNCAST(view, view, sc_msg_view);
}

static inline void sc_msg_view_ctor(struct sc_msg_view *view, struct sc_plugin *plugin, struct sc_msg *msg, GtkWidget *window)
{
	view->msg = sc_msg_ref(msg);
	sc_view_ctor(&view->view, plugin->ops->msg_view_del, window);
}

#include <assert.h>
static inline void sc_msg_view_dtor(struct sc_msg_view *view)
{
	sc_view_dtor(&view->view);
	if (view->msg) {
		sc_msg_unref(view->msg);
		view->msg = NULL;
	}
}

struct sc_dir_view {
	struct sc_view view;
	struct mdirb *mdirb;
};

static inline struct sc_dir_view *view2dir_view(struct sc_view *view)
{
	return DOWNCAST(view, view, sc_dir_view);
}

static inline void sc_dir_view_ctor(struct sc_dir_view *view, struct sc_plugin *plugin, struct mdirb *mdirb, GtkWidget *window)
{
	view->mdirb = mdirb;
	sc_view_ctor(&view->view, plugin->ops->dir_view_del, window);
}

static inline void sc_dir_view_dtor(struct sc_dir_view *view)
{
	sc_view_dtor(&view->view);
}

extern LIST_HEAD(sc_plugins, sc_plugin) sc_plugins;

void sc_plugin_register(struct sc_plugin *);

#endif
