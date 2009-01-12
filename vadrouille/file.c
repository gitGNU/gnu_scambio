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
#include "file.h"

/*
 * File messages are just plain sc_msg
 */

static struct sc_plugin file_plugin;

static struct sc_msg *file_msg_new(struct mdirb *mdirb, struct header *h, mdir_version version)
{
	struct sc_msg *msg = Malloc(sizeof(*msg));
	if_fail (sc_msg_ctor(msg, mdirb, h, version, &file_plugin)) {
		free(msg);
		return NULL;
	}
	return msg;
}

static void file_msg_del(struct sc_msg *msg)
{
	sc_msg_dtor(msg);
	free(msg);
}

/*
 * View a File
 */

static struct sc_msg_view *file_view_new(struct sc_msg *msg)
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

static char *file_msg_descr(struct sc_msg *msg)
{
	struct header_field *hf = header_find(msg->header, SC_NAME_FIELD, NULL);
	return g_markup_printf_escaped("File <i>%s</i>", hf ? hf->value:"NoName");
}

static char *file_msg_icon(struct sc_msg *msg)
{
	(void)msg;
	return GTK_STOCK_DND;
}

/*
 * Init
 */

static struct sc_plugin_ops const ops = {
	.msg_new          = file_msg_new,
	.msg_del          = file_msg_del,
	.msg_descr        = file_msg_descr,
	.msg_icon         = file_msg_icon,
	.msg_view_new     = file_view_new,
	.msg_view_del     = NULL,
	.dir_view_new     = NULL,
	.dir_view_del     = NULL,
	.dir_view_refresh = NULL,
};
static struct sc_plugin file_plugin = {
	.name = "file",
	.type = SC_FILE_TYPE,
	.ops = &ops,
	.nb_global_functions = 0,
	.global_functions = {},
	.nb_dir_functions = 0,
	.dir_functions = {},
};

void file_init(void)
{
	sc_plugin_register(&file_plugin);
}
