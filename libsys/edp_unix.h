#ifndef EDP_UNIX_H_080616
#define EDP_UNIX_H_080616

#include "edp.h"

typedef int (edp_unix_fd_setter)(fd_set *rset, fd_set *wset);
typedef void (edp_unix_reader)(fd_set *rset);
typedef void (edp_unix_writer)(fd_set *wset);

struct edp_module *edp_unix_module_register(char const *name, edp_unix_fd_setter *, edp_unix_reader *, edp_unix_writer *);
void edp_unix_module_unregister(struct edp_module *);

#endif
