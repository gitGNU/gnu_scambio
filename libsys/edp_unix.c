#include <sys/select.h>
#include <string.h>
#include <errno.h>
#include <object.h>
#include "edp_unix.h"
#include "loger.h"

/*
 * Definitions
 */

struct edp_unix_module {
	struct edp_module edp_module;
	edp_unix_fd_setter *fd_set;
	edp_unix_reader *reader;
	edp_unix_writer *writer;
};

/*
 * (De)Initialization
 */

int edp_arch_begin(void)
{
	return 0;
}

void edp_arch_deinit(void)
{
}

/*
 * Event Selection and Handling
 * Each unix module must work with a set of file descriptor.
 * Event is then the fd being ready to read or write.
 */

void edp_arch_service(void)
{
	fd_set rset, wset;
	int max_fd = 0;
	FD_ZERO(&rset);
	FD_ZERO(&wset);
	struct edp_module *mod;
	LIST_FOREACH(mod, edp_modules, entry) {
		struct edp_unix_module *umod = DOWNCAST(mod, edp_module, edp_unix_module);
		printl(LOG_DEBUG, "Setting fd set for module '%s'", mod->name);
		int max = umod->fd_setter(&rset, &wset);
		if (max > max_fd) max_fd = max;
	}
	int sel = select(max_fd+1, &rset, &wset, NULL, NULL);
	switch (sel) {
		case -1:
			if (errno != EINTR) {
				printl(LOG_ERR, "edp_unix_service: select: %s", strerror(errno));
			}
			break;
		case 0:
			break;
		default:
			LIST_FOREACH(mod, edp_modules, entry) {
				struct edp_unix_module *umod = DOWNCAST(mod, edp_module, edp_unix_module);
				if (umod->writer) umod->writer(&wset);
				if (umod->reader) umod->reader(&rset);
			}
			break;
	}
}

/*
 * Module (Un)registration
 */

static struct edp_module *edp_unix_module_ctor(struct edp_unix_module *umod, char const *name, edp_unix_fd_setter *fd_setter, edp_unix_reader *reader, edp_unix_writer *writer)
{
	if (0 != edp_module_ctor(&umod->edp_module)) return -1;
	umod->fd_setter = fd_setter;
	umod->reader = reader;
	umod->writer = writer;
	return 0;
}

struct edp_module *edp_unix_module_register(char const *name, edp_unix_fd_setter *fd_setter, edp_unix_reader *reader, edp_unix_writer *writer)
{
	// TODO: check that the mod is not already loaded ?
	struct edp_unix_module *umod = malloc(*umod);
	if (! umod) return NULL;
	if (0 != edp_unix_module_ctor(&umod->edp_module, name, fd_setter, reader, writer)) {
		free(umod);
		return NULL;
	}
}

void edp_unix_module_unregister(struct edp_module *mod)
{
	struct edp_unix_module *umod = DOWNCAST(mod, edp_module, edp_unix_module);
	edp_unix_module_dtor(umod);
	free(umod);
}
