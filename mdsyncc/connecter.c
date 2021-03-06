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
#include "scambio/cnx.h"
#include "auth.h"
#include "mdsyncc.h"

/*
 * Threads share some structures related to the shared socket :
 * First the socket itself
 */

struct mdir_cnx cnx;
static struct mdir_syntax syntax;

/* Connecter try to establish the connection, and keep trying once in a while
 * untill success, then spawn reader and writer until one of them returns, when
 * it kills the remaining one, closes the connection, and is ready to be restarted.
 */
void *connecter_thread(void *arg)
{
	(void)arg;
	debug("Starting connecter");
	if_fail (mdir_cnx_ctor_outbound(&cnx, &syntax, conf_get_str("SC_MDIRD_HOST"), conf_get_str("SC_MDIRD_PORT"), mdir_user_name(user))) {
		error_clear();
		return NULL;
	}
	// TODO: wait until completion if assynchronous ?
	reader_pthid = pth_spawn(PTH_ATTR_DEFAULT, reader_thread, NULL);
	writer_pthid = pth_spawn(PTH_ATTR_DEFAULT, writer_thread, NULL);
	while (reader_pthid && writer_pthid) {
/*
		pth_event_t ev_r = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD, reader_pthid);
		pth_event_t ev_w = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD, writer_pthid);
		pth_event_t ev_ring = pth_event_concat(ev_r, ev_w, NULL);
		int nonpending = pth_wait(ev_ring);	// FIXME: does not wait, and returns SELECT ?
		debug("%d event(s) occured", nonpending);
		pth_event_t ev = ev_ring;
		do {
			if (
				pth_event_typeof(ev) == (PTH_EVENT_TID|PTH_UNTIL_TID_DEAD) &&
				pth_event_status(ev) == PTH_STATUS_OCCURRED
			) {
				debug("occuring pth event %u", (unsigned)pth_event_typeof(ev));
				//assert(pth_event_typeof(ev) == (PTH_EVENT_TID|PTH_UNTIL_TID_DEAD));	// FIXME:  FAILS
				pth_t dead_one;
				pth_event_extract(ev, &dead_one);
				if (dead_one == reader_pthid) reader_pthid = NULL;
				if (dead_one == writer_pthid) writer_pthid = NULL;
			}
			ev = pth_event_walk(ev, PTH_WALK_NEXT);
		} while (ev != ev_ring);
		pth_event_free(ev_ring, PTH_FREE_ALL);
*/
		pth_sleep(5);
	}
	if (reader_pthid) (void)pth_cancel(reader_pthid);
	if (writer_pthid) (void)pth_cancel(writer_pthid);
	mdir_cnx_dtor(&cnx);
	debug("Ending connecter");
	return NULL;
}

/*
 * Init
 */

void connecter_begin(void)
{
	if_fail (mdir_syntax_ctor(&syntax, true)) return;
	// Register all queries (for answer)
	static struct mdir_cmd_def defs[] = {
		MDIR_CNX_ANSW_REGISTER(kw_sub,   finalize_sub),
		MDIR_CNX_ANSW_REGISTER(kw_unsub, finalize_unsub),
		MDIR_CNX_ANSW_REGISTER(kw_put,   finalize_put),
		MDIR_CNX_ANSW_REGISTER(kw_rem,   finalize_rem),
		MDIR_CNX_ANSW_REGISTER(kw_quit,  finalize_quit),
		MDIR_CNX_ANSW_REGISTER(kw_auth,  finalize_auth),
		{
			.keyword = kw_patch, .cb = patch_service, .nb_arg_min = 4, .nb_arg_max = 4,
			.nb_types = 4, .types = { CMD_STRING, CMD_INTEGER, CMD_INTEGER, CMD_STRING, }, .negseq = false,
		},
	};
	for (unsigned t=0; t<sizeof_array(defs); t++) {
		if_fail (mdir_syntax_register(&syntax, defs+t)) return;
	}
}

void connecter_end(void)
{
	mdir_syntax_dtor(&syntax);
}
