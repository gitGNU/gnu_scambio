#include <stdlib.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/header.h"

int main(void)
{
	pth_init();
	log_begin(NULL, NULL);
	error_begin();
	atexit(error_end);
	log_level = 5;
	while (! is_error()) {
		struct header *h;
		if_fail (h = header_new()) break;
		if_fail (header_read(h, 0)) break;
		header_debug(h);
	}
	pth_kill();
	return EXIT_SUCCESS;
}
