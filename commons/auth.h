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
#ifndef AUTH_H_081011
#define AUTH_H_081011
/* The server needs to authenticate each user and the communication needs to be
 * crypted - communication which can be UDP.
 * Pre-shared key symetric cyphering is all we need.
 * Secrets are stored in a directory containing a file per key, named with the user
 * whom its the key (on the server as well as the client), so that it's easy to add
 * accounts.
 *
 * This common routines read this directory for the key, encrypt and decrypt
 * messages, and so on.
 * It would be a good idea to compress the flow _before_ encrypting.
 * Also, the same keys should serve to discuss with the file server(s), which then
 * must also know all users keys.
 *
 * Some users may have some alias fields : they are equivalent to these other users.
 */

#include <stdbool.h>

struct mdir_user;
void auth_init(void);
struct mdir_user *mdir_user_load(char const *name);
struct header *mdir_user_header(struct mdir_user *user);
char const *mdir_user_name(struct mdir_user *user);

/* Permissions are stored in each dirId (as a header file).
 * This file is updated each time the directory is patched with a header of type
 * "permissions". Who can sent this patch ? Only one of the "admins" of the dirId.
 * Who'is the admins ? It's said in the previous permission header (allow-read/write,
 * deny-read/write are completed with allow-admin/deny-admin). This is thus possible,
 * with a single simple command, to change perm or ownership of a dirId.
 * If no perm file exists in the dirId, or if no admin is declared, then it's owned
 * by special user "admin".
 */

/* Tells weither the given user is the same as group, or if user is reachable from
 * group aliases. If the groupname is unknown, return if_unknown.
 */
bool mdir_user_is_in_group(struct mdir_user *user, char const *groupname, bool if_unknown);

/* Tells if a given header gives read permission to this user.
 */
bool mdir_user_can_read(struct mdir_user *user, struct header *header);

/* Same for write permissions.
 */
bool mdir_user_can_write(struct mdir_user *user, struct header *header);

/* Same for admin permissions.
 */
bool mdir_user_can_admin(struct mdir_user *user, struct header *header);

#endif
