#include <stdbool.h>
#include "jnl.h"

static bool client_needs_patch(struct subscription *sub)
{
	return sub->jnl != NULL;
}

static int send_next_patch(struct subscription *sub)
{
	int err = 0;
	struct varbuf vb;
	if (0 != (err = varbuf_ctor(&vb, 10000, true))) return err;
	// varbuf append "PATCH "
	// varbuf append relative path (dir)
	// varbuf append sub->version and "\n"
	// then read sub->jnl->fd from offset sub->offset until end of headers
	// (ie line begining by '%?', see jnl_offset_of()). Then, Write this varbuf to sub->env->fd
	// update sub->jnl and sub->offset
	//
	varbuf_dtor(&vb);
}

void *subscription_thread(void *sub_)
{
	// FIXME: we need a pointer from subscription to the client's env.
	// AND a RW lock to the FD so that no more than one thread writes it concurrently.
	struct subscription *sub = sub_;
	debug("new thread for subscription @p", sub);
	while (client_needs_patch(sub)) {
		// TODO: take writer lock on env->fd (which we need)
		// Write a patch
		
		// release lock
	}
	// TODO
	return NULL;
}

