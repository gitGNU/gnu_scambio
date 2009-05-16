#ifndef MAIN_H_090509
#define MAIN_H_090509

#include <pth.h>
#include "scambio/mdir.h"

struct strib_mdir {
	struct mdir mdir;
	struct header *conf;	// secret message with last conf, last conf version, and last processed version
	mdir_version last_conf_version;
	mdir_version last_done_version;
	pth_t thread;	// may be NULL
	struct mdir_cursor cursor;
};

#define STRIB_SECRET_FILE ".stribution"

// Field names used in strib headers
#define STRIB_LAST_CONF_VERSION "strib-last-conf-version"
#define STRIB_LAST_DONE_VERSION "strib-last-done-version"

#endif
