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
#include <pth.h>
#include "scambio.h"
#include "cnx.h"
#include "mdirc.h"

/*
 * Threads share some structures related to the shared socket :
 * First the socket itself
 */

struct cnx_client cnx;

/* Connecter try to establish the connection, and keep trying once in a while
 * untill success, then spawn reader and writer until one of them returns, when
 * it kills the remaining one, closes the connection, and is ready to be restarted.
 */
void *connecter_thread(void *arg)
{
	debug("Starting connecter");
	(void)arg;
	cnx_client_ctor(&cnx, conf_get_str("MDIRD_HOST"), conf_get_str("MDIRD_PORT"));
	on_error return NULL;
	// TODO: wait until completion if assynchronous ?
	reader_pthid = pth_spawn(PTH_ATTR_DEFAULT, reader_thread, NULL);
	writer_pthid = pth_spawn(PTH_ATTR_DEFAULT, writer_thread, NULL);
	pth_event_t ev_r = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD, reader_pthid);
	pth_event_t ev_w = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD, writer_pthid);
	pth_event_t ev_ring = pth_event_concat(ev_r, ev_w, NULL);
	while (reader_pthid && writer_pthid) {
		(void)pth_wait(ev_ring);
		pth_event_t ev_occurred;
		// TODO: check that pth_event_walk returns NULL at end of ring
		while (NULL != (ev_occurred = pth_event_walk(ev_ring, PTH_WALK_NEXT|PTH_UNTIL_OCCURRED))) {
			assert(pth_event_typeof(ev_occurred) == (PTH_EVENT_TID|PTH_UNTIL_TID_DEAD));	// FIXME:  FAILS
			pth_t dead_one;
			pth_event_extract(ev_occurred, &dead_one);
			if (dead_one == reader_pthid) reader_pthid = NULL;
			if (dead_one == writer_pthid) writer_pthid = NULL;
		}
	}
	pth_event_free(ev_ring, PTH_FREE_ALL);
	if (reader_pthid) (void)pth_cancel(reader_pthid);
	if (writer_pthid) (void)pth_cancel(writer_pthid);
	cnx_client_dtor(&cnx);
	debug("Ending connecter");
	return NULL;
}

