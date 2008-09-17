/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Interface to folders.
 * Beware that many threads may access those files concurrently,
 * although they are non preemptibles.
 */
#ifndef MDIR_H_080912
#define MDIR_H_080912

#include <stdbool.h>
#include <pth.h>
#include <limits.h>
#include <inttypes.h>
#include <scambio/queue.h>

enum mdir_action { MDIR_ADD, MDIR_REM };

typedef uint64_t mdir_version;
#define PRIversion PRIu64

struct header;
void mdir_begin(void);
void mdir_end(void);

// create a new mdir
struct mdir *mdir_create(void);

// add/remove a header into a directory
void mdir_patch(struct mdir *, enum mdir_action, struct header *);

// listeners
struct mdir_listener {
	struct mdir_listener_ops {
		void (*del)(struct mdir_listener *, struct mdir *);	// must unregister if its registered
		void (*notify)(struct mdir_listener *, struct mdir *, struct header *h);
	} const *ops;
	LIST_ENTRY(mdir_listener) entry;
};
static inline void mdir_listener_ctor(struct mdir_listener *l, struct mdir_listener_ops const *ops)
{
	l->ops = ops;
}
static inline void mdir_listener_dtor(struct mdir_listener *l) { (void)l; }
void mdir_register_listener(struct mdir *mdir, struct mdir_listener *l);
void mdir_unregister_listener(struct mdir *mdir, struct mdir_listener *l);

// returns the mdir for this name (which may countains "/"s, but must exists)
struct mdir *mdir_lookup(char const *name);

// name is not allowed to use '/' (ie no lookup is performed)
void mdir_link(struct mdir *parent, char const *name, struct mdir *child);
void mdir_unlink(struct mdir *parent, char const *name);

// returns the header, action and version following the given version
// or NULL if no other patches are found
struct header *mdir_read_next(struct mdir *, mdir_version *, enum mdir_action *);

// returns the last version of this mdir
mdir_version mdir_last_version(struct mdir *);
char const *mdir_id(struct mdir *);

#endif
