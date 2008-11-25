#ifndef C2L_H_081125
#define C2L_H_081125
#include "mdsyncc.h"

struct c2l_map {
	LIST_ENTRY(c2l_map) entry;
	mdir_version central, local;
};

struct c2l_map *c2l_new(struct c2l_maps *list, mdir_version central, mdir_version local);
void c2l_del(struct c2l_map *c2l);
struct c2l_map *c2l_search(struct c2l_maps *list, mdir_version central);

#endif
