#include <stdlib.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "c2l.h"
#include "mdsyncc.h"

struct c2l_map *c2l_new(struct c2l_maps *list, mdir_version central, mdir_version local)
{
	debug("local version %"PRIversion" corresponds to central version %"PRIversion, local, central);
	struct c2l_map *c2l = malloc(sizeof(*c2l));
	if (! c2l) with_error(ENOMEM, "malloc(c2l)") return NULL;
	c2l->central = central;
	c2l->local = local;
	LIST_INSERT_HEAD(list, c2l, entry);
	return c2l;
}

void c2l_del(struct c2l_map *c2l)
{
	LIST_REMOVE(c2l, entry);
	free(c2l);
}

struct c2l_map *c2l_search(struct c2l_maps *list, mdir_version central)
{
	struct c2l_map *c2l;
	LIST_FOREACH(c2l, list, entry) {
		if (c2l->central == central) return c2l;
	}
	return NULL;
}
