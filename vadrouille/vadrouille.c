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
#include "mail.h"

/*
 * Plugins
 */

struct sc_plugins sc_plugins = LIST_HEAD_INITIALIZER(&sc_plugins);

void sc_plugin_register(struct sc_plugin *plugin)
{
	LIST_INSERT_HEAD(&sc_plugins, plugin, entry);
}

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

int main(int nb_args, char *args[])
{
	if_fail (init("vadrouille", nb_args, args)) return EXIT_FAILURE;
	if_fail (ccnx_init()) return EXIT_FAILURE;
	if_fail (mdirb_init()) return EXIT_FAILURE;
	if_fail (sc_msg_init()) return EXIT_FAILURE;
	if_fail (mail_init()) return EXIT_FAILURE;
	if_fail (browser_init()) return EXIT_FAILURE;

	struct browser *browser = browser_new("/");
	on_error return EXIT_FAILURE;
	exit_when_closed(browser->window);

	gtk_main();
	return EXIT_SUCCESS;
}

