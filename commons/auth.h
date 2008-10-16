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
 * Some special users have no key but an alias list : they are equivalent to unix
 * groups.
 * Also every directory have an optional .write and .read files describing who can(not)
 * write or read this directory. This is a list of allow/deny commands followed by a
 * list of users, ended by an implied allow all or deny all depending on the global
 * policy, which is a parameter of mdird (thus when there is no file at all the
 * write/read rights is given by global policy).
 * Beware that the conf must be preparsed but renewed whenever a file change, including
 * a user group file - so the groups must not be expended or the dependancy over the
 * group file will be missed.
 */

struct mdir_user;
void auth_begin(void);
void auth_end(void);
struct mdir_user *mdir_user_load(char const *name);

#endif
