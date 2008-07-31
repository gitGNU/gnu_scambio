#include <assert.h>
#include <pth.h>
#include "scambio.h"
#include "cnx.h"
#include "main.h"

/* Connecter try to establish the connection, and keep trying once in a while
 * untill success, then spawn reader and writer until one of them return, when
 * it kills the remaining one, close the connection, and restart.
 */
void *connecter_thread(void *arg)
{
	(void)arg;
	pth_t reader, writer;
	if (0 != cnx_client_ctor(&cnx, conf_get_str("MDIRD_SERVER"), conf_get_str("MDIRD_PORT"))) return NULL;
	// TODO: wait until completion if assynchronous ?
	reader = pth_spawn(PTH_ATTR_DEFAULT, reader_thread, NULL);
	writer = pth_spawn(PTH_ATTR_DEFAULT, writer_thread, NULL);
	pth_event_t ev_r = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD, reader);
	pth_event_t ev_w = pth_event(PTH_EVENT_TID|PTH_UNTIL_TID_DEAD, writer);
	pth_event_t ev_ring = pth_event_concat(ev_r, ev_w, NULL);
	while (reader && writer) {
		(void)pth_wait(ev_ring);
		pth_event_t ev_occurred;
		// TODO: check that pth_event_walk returns NULL at end of ring
		while (NULL != (ev_occurred = pth_event_walk(ev_ring, PTH_WALK_NEXT|PTH_UNTIL_OCCURRED))) {
			assert(pth_event_typeof(ev_occurred) == (PTH_EVENT_TID|PTH_UNTIL_TID_DEAD));
			pth_t dead_one;
			pth_event_extract(ev_occurred, &dead_one);
			if (dead_one == reader) reader = NULL;
			if (dead_one == writer) writer = NULL;
		}
	}
	pth_event_free(ev_ring, PTH_FREE_ALL);
	if (reader) (void)pth_cancel(reader);
	if (writer) (void)pth_cancel(writer);
	cnx_client_dtor(&cnx);
	return NULL;
}

