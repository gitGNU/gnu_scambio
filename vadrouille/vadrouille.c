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
#include "scambio/header.h"
#include "scambio.h"
#include "merelib.h"
#include "auth.h"
#include "misc.h"
#include "vadrouille.h"
#include "mdirb.h"
#include "browser.h"
// Plugins (TODO)
#include "mail.h"
#include "calendar.h"
#include "contact.h"
#include "bookmark.h"

/*
 * Plugins
 */

struct sc_plugins sc_plugins = LIST_HEAD_INITIALIZER(&sc_plugins);

void sc_plugin_register(struct sc_plugin *plugin)
{
	LIST_INSERT_HEAD(&sc_plugins, plugin, entry);
}

// Called whenever a view is deleted
void unref_view(GtkWidget *widget, gpointer data)
{
	struct sc_view *const view = (struct sc_view *)data;
	debug("unref view@%p, window = %p", view, view->window);
	if (view->window) {
		assert(widget == view->window);
		view->window = NULL;
	}
	view->del(view);
}

// Called whenever a dir_view's mdirb changes
void listener_refresh_for_dirview(struct mdirb_listener *listener, struct mdirb *mdirb)
{
	(void)mdirb;
	struct sc_dir_view *const view = listener2dir_view(listener);
	assert(mdirb == view->mdirb);
	if (view->plugin->ops->dir_view_refresh) {
		view->plugin->ops->dir_view_refresh(view);
	}
}

extern inline void sc_view_ctor(struct sc_view *, void (*del)(struct sc_view *), GtkWidget *window);
extern inline void sc_view_dtor(struct sc_view *);
extern inline struct sc_msg_view *view2msg_view(struct sc_view *);
extern inline void sc_msg_view_ctor(struct sc_msg_view *, struct sc_plugin *, struct sc_msg *, GtkWidget *window);
extern inline void sc_msg_view_dtor(struct sc_msg_view *);
extern inline struct sc_dir_view *view2dir_view(struct sc_view *);
extern inline struct sc_dir_view *listener2dir_view(struct mdirb_listener *);
extern inline void sc_dir_view_ctor(struct sc_dir_view *, struct sc_plugin *, struct mdirb *, GtkWidget *window);
extern inline void sc_dir_view_dtor(struct sc_dir_view *);

/*
 * Init
 */

struct chn_cnx ccnx;

void ccnx_init(void)
{
	if_fail (chn_init(false)) return;
	// TODO: we could also put the filed host/port on the resource line, and use a pool of ccnx ?
	conf_set_default_str("SC_FILED_HOST", "localhost");
	conf_set_default_str("SC_FILED_PORT", DEFAULT_FILED_PORT);
	on_error return;
	if_fail (chn_cnx_ctor_outbound(&ccnx, conf_get_str("SC_FILED_HOST"), conf_get_str("SC_FILED_PORT"), conf_get_str("SC_USERNAME"))) return;
}

/*
 * Main
 */

static gboolean refresh_alarm(gpointer user_data)
{
	struct browser *browser = (struct browser *)user_data;
	browser_refresh(browser);
	return TRUE;
}

int main(int nb_args, char *args[])
{
	if_fail (init("vadrouille", nb_args, args)) return EXIT_FAILURE;
	if_fail (ccnx_init()) return EXIT_FAILURE;
	if_fail (mdirb_init()) return EXIT_FAILURE;
	if_fail (sc_msg_init()) return EXIT_FAILURE;
	if_fail (mail_init()) return EXIT_FAILURE;
	if_fail (calendar_init()) return EXIT_FAILURE;
	if_fail (contact_init()) return EXIT_FAILURE;
	if_fail (browser_init()) return EXIT_FAILURE;
	if_fail (bookmark_init()) return EXIT_FAILURE;

	struct browser *browser = browser_new("/");
	on_error return EXIT_FAILURE;
	exit_when_closed(browser->window);
	g_timeout_add_seconds(1, refresh_alarm, browser);

	gtk_main();
	return EXIT_SUCCESS;
}

