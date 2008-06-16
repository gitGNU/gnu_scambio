/* OS independant part of EDP : callback memorization.
 */
#include <stdbool.h>
#include "edp.h"

/*
 * Definitions
 */

static int exit_code;
static bool exit_asked;

/*
 * (De)Initialization
 */

int edp_begin(void)
{
	exit_asked = false;
	return edp_arch_begin();
}

void edp_end(void)
{
	edp_arch_end();
}

/*
 * Main loop
 */

int edp_run(void)
{
	while (! exit_asked) {
		edp_arch_service();
		struct edp_event *ev;
		while (NULL != (ev = LIST_FIRST(edp_events))) {
			// TODO: Send all events

			LIST_REMOVE(ev, entry);
		}
	}
	return exit_code;
}

void edp_exit(int exit_code_)
{
	if (! exit_asked) {
		exit_asked = true;
		exit_code = exit_code_;
	}
}

