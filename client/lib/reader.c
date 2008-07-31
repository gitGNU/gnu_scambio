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
#include "main.h"
#include "scambio.h"

/* The reader listens for commands.
 * On a put response, it removes the temporary filename stored in the action
 * (the actual meta file will be synchronized independantly from the server).
 * On a rem response, it removes the temporary filename (same remark as above).
 * On a sub response, it moves the subscription from subscribing to subscribed.
 * On an unsub response, it deletes the unsubcribing subscription.
 * On a patch for addition, it creates the meta file (under the digest name)
 * and updates the version number. The meta may already been there if the update of
 * the version number previously failed.
 * On a patch for deletion, it removes the meta file and updates the version number.
 * Again, the meta may already have been removed if the update of the version number
 * previously failed.
 */

void *reader_thread(void *args)
{
	(void)args;
	debug("starting reader thread");
	return NULL;
}

int reader_begin(void)
{
	return 0;
}

void reader_end(void)
{
}
