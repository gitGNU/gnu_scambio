/* System dependant functions for Event Driven Programms
 *
 * Allows programs and modules to rely on those abstractions :
 *
 * - An asynchronous interface based on callbacks that are registered
 *   for each action (callback may be called several time).
 *
 * - A mean to register modules that will implement more functionalities (ie
 *   register new commands that will send new callbacks). Merely, modules can
 *   store callbacks and find them later given a module key and action key, and
 *   then call them.
 *
 * First call edp_init, then register all your modules, then call edp_run which
 * will enter the main loop (event selection + handling).
 * edp_exit may be called from any handler to have the main loop quit with given
 * code. Notice that <0 code are reserved for edp use.
 * edp_end must be called before quitting the programm or calling edp_init once
 * again.
 */

#ifndef EDP_H_080616
#define EDP_H_080616
#include <queue.h>
#include "scambio.h"

// User API

// define here <0 exit_code

int edp_begin(void);
int edp_run(void);
void edp_exit(int exit_code);
void edp_end(void);

// For modules

struct edp_module_ops {
	void (*del)(struct edp_module *module);
};
struct edp_module {
	struct edp_module_ops const *ops;
	LIST_ENTRY(edp_module) entry;
	char *name;
};
LIST_HEAD(edp_modules, edp_module) edp_modules;

int edp_module_ctor(struct edp_module *mod, char const *name);	// Called by arch specific module registering function
void edp_module_dtor(struct edp_module *mod);

// Arch specific must implement this

int edp_arch_init(void);	// called by edp_init()
void edp_arch_end(void);	// called by edp_end()
void edp_arch_service(void);	// called endlessly by edp_run()

#endif
