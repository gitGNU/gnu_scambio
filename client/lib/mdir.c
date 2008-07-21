#include <errno.h>
#include "scambio.h"
#include "mdir.h"

struct mdir {
	int count;
};

/*
 * Get a mdir reference
 */

int mdir_get(struct mdir **mdir, char const *path)
{
	debug("mdir_get(path='%s')", path);
	*mdir = NULL;
	return -ENOENT;
}

void mdir_unref(struct mdir *mdir)
{
	mdir->count--;
}

