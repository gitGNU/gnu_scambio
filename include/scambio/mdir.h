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

// create a new mdir (you will want to mdir_link() it somewhere)
struct mdir *mdir_create(void);

// add/remove a header into a mdir
// do not use this in plugins : only the server decides how and when to apply a patch
// plugins use mdir_patch_request instead
void mdir_patch(struct mdir *, enum mdir_action, struct header *);

// ask for the addition of this patch to the mdir
// actually the patch will be saved in a tempfile where it will be found by the client
// synchronizer mdirc, sent to the server whenever possible, then removed (!), then
// synchronized down hopefully. So this does not require a connection to mdird.
// Problems :
// - between the file is removed and received from the server, the message appears to
// vanish from the client. Not very problematic in practice.
// - how does mdirc manage to delete the tempfile ? It uses the returned version,
// which is in another 'space' the journal version, but serve a similar purpose.
mdir_version mdir_patch_request(struct mdir *, enum mdir_action, struct header *);

// abort a patch request if its not already aborted.
// If the patch found is of type: dir, unlink also the dir from its parent.
void mdir_patch_request_abort(struct mdir *, enum mdir_action, mdir_version version);

// listeners
// will be called whenever a patch is applied to the mdir
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

// returns the mdir for this name (relative to mdir_root, must exists)
struct mdir *mdir_lookup(char const *name);

// FIXME FIRST: won't it be simpler if these (un)link functions takes a header, add a "type: dir"
// to it, and patch(_request) it in addition to symlinking the dirs ?
// link a mdir into another one, so that the two apears in a parent/child relationship.
// the corresponding patch to the parent is _not_ performed. Call this when you receive
// the relevant patch only (ie, plugins uses mdir_(un)link_request instead).
// name is not allowed to start with a '.' nor to countain '/'.
void mdir_link(struct mdir *parent, char const *name, struct mdir *child);
void mdir_unlink(struct mdir *parent, char const *name);

// ask for a new link between two mdirs.
// (actually this will create a link under the dotted name, that will be moved when
// the proper patch will be eventually received).
// this does not prevent you from calling mdir_patch_request with the proper patch for it.
// If these were also creating the relevant patch, the versions returned would be those of these
// patches, into which the name of the directory could be found.
mdir_version mdir_link_request(struct mdir *parent, char const *name, struct mdir *child);
mdir_version mdir_unlink_request(struct mdir *parent, char const *name);

// list all subdirectories of a mdir
// confirmed, ie in journal, or unconfirmed.
void mdir_link_list(struct mdir *, bool confirmed, bool unconfirmed, void (*cb)(struct mdir *parent, struct mdir *child, bool confirmed, mdir_version version));

// list all patches of a mdir
// (will also list unconfirmed patches)
void mdir_patch_list(struct mdir *, bool confirmed, bool unconfirmed, void (*cb)(struct mdir *, struct header *, enum mdir_action action, bool confirmed, mdir_version version));

// returns the header, action and version following the given version
// or NULL if no other patches are found
struct header *mdir_read_next(struct mdir *, mdir_version *, enum mdir_action *);

// returns the last version of this mdir
mdir_version mdir_last_version(struct mdir *);
char const *mdir_id(struct mdir *);

#endif
