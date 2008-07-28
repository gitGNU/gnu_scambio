/* The client lib first connect to a mdird, then spawns two threads : one that writes and
 * one that reads the socket.
 */

#include <pth.h>
#include <errno.h>
#include "cnx.h"

/* Those two threads share some structures related to the shared socket :
 * lists of pending sub, unsub, put and rem commands.
 *
 * The writer will add entries to these lists while the reader will remove them once completed,
 * and perform the proper final action for these commands.
 */

#include <limits.h>
#include <time.h>
#include "queue.h"
struct subscription {
	enum subscription_state { SUBSCRIBING, SUBSCRIBED, UNSUBSCRIBING } state;
	LIST_ENTRY(sub_command) entry;	// the list depends on the state
	char rel_path[PATH_MAX];	// folder's path relative to root dir
	long long seq;	// irrelevant if SUBSCRIBED
	time_t send;	// idem
};
static LIST_HEAD(subscriptions, subscription) subcribing, subscribed, unsubscribing;
static pth_rwlock_t subscribing_lock, subscribed_lock, unsubscribing_lock;

struct putrem_command {
	LIST_ENTRY(putrem_command) entry;
	char meta_path[PATH_MAX];	// absolute path to the meta file
	long long seq;
	time_t send;
};
static LIST_HEAD(putrem_commands, putrem_command) put_commands, rem_commands;
static pth_rwlock_t put_commands_lock, rem_commands_lock;

/* The writer thread works by traversing a directory tree from a given root.
 * - wait until root queue not empty ;
 * - unqueue the oldest one, and process the directory recursively :
 *   - read its .hide file and parse it ;
 *   - For each folder entry :
 *     - if the entry is a directory :
 *       - if we are in a '.put' or a '.rem' directory, ignore it ;
 *       - if it's a .put directory, recurse with flag 'in_put' set ;
 *       - if it's a .rem directory, recurse with flag 'in_rem' set ;
 *       - if it's not on the hide list :
 *         - if it's not already subscribed, or if the subscription command timed out,
 *           subscribe to it ;
 *         - anyway, recurse.
 *       - so this is on the hide list :
 *         - if we are tracking this folder, or if the pending unsubscription timed out,
 *           send an unsub command _and_recurse_ ;
 *     - otherwise it's an ordinary file :
 *       - if we are neither in a '.put' not a '.rem' directory, ignore it ;
 *       - send the appropriate command if it was not already sent or if
 *         the previous one timed out ;
 * After each entry is inserted a schedule point so other threads can execute.
 * Also, for security, a max depth is fixed to 30.
 *
 * When a plugin add/remove content or change the subscription state, it adds
 * the location of this action into the root queue, as well as a flag telling if the
 * traversall should be recursive or not. For instance, adding a message means
 * adding a tempfile in the folder's .put directory and pushing this folder
 * into the root queue while removing one means linking its meta onto the .rem
 * directory (the actual meta will be deleted on reception of the suppression patch).
 */
#include <sys/types.h>
#include <dirent.h>

static bool always_skip(char const *name)
{
	return
		0 == strcmp(name, ".") ||
		0 == strcmp(name, "..");
}

#define PUTDIR_NAME ".put"
#define REMDIR_NAME ".rem"
enum dir_type { PUT_DIR, REM_DIR, FOLDER_DIR };
static int writer_step_rec(char path[], enum dir_type dir_type, int depth)
{
#	define MAX_DEPTH 30;
	if (depth >= MAX_DEPTH) return -ELOOP;
	int err;
	struct hide_cfg *hide_cfg;
	if (0 != (err = hide_cfg_get(&hide_cfg, path))) goto q0;	// load the .hide file
	DIR *dir = opendir(path);
	if (! dir) {
		err = -errno;
		goto q1;
	}
	struct dirent *dirent;
	errno = 0;
	while (!err && NULL != (dirent = readdir(dir))) {
		if (always_skip(dirent->d_name)) continue;
		pth_yield(NULL);
		struct stat statbuf;
		if (0 != stat(dirent->d_name, &statbuf)) {
			err = -errno;
			break;
		}
		path_push(path, dirent->d_name);
		if (S_ISDIR(statbuf.st_mode)) {
			if (dir_type == PUT_DIR || dir_type == REM_DIR) continue;
			if (0 == strcmp(dirent->d_name, PUTDIR_NAME)) {
				err = writer_step_rec(path, PUT_DIR, depth+1);
			} else if (0 == strcmp(dirent->d_name, REMDIR_NAME)) {
				err = writer_step_rec(path, REM_DIR, depth+1);
			} else if (show_this_dir(hide_cfg, dirent->d_name)) {
				err = try_subscribe(path);
				if (! err) err = writer_step_rec(path, FOLDER_DIR, depth+1);
			} else {	// hide this dir
				err = try_unsubscribe(path);
				if (! err) err = writer_step_rec(path, FOLDER_DIR, depth+1);
			}
		} else {	// dir entry is not itself a directory
			if (dir_type == FOLDER_DIR) continue;
			err = try_command(dir_type, path);
		}
		path_pop(path);
	}
	if (!err && errno) {
		err = -errno;
	}
	closedir(dir);
q1:
	hide_cfg_release(&hide_cfg);
q0:
	return err;
}

/* The reader listens for commands.
 * On a put response, it removes the temporary filename stored in the action
 * (the actual meta file will be synchronized independantly from the server).
 * On a rem response, it removes the temporary filename (same remark as above).
 * On a sub response, it moves the subscription from subscribing to subscribed.
 * On an unsub response, it deletes the unsubcribing subscription.
 * On a patch for addition, it creates the meta file (under the digest name)
 * and updates the version number. The meta may already been there if the update of
 * the version number previously failed.
 * On a patch for deletion, it removes the meta file and updates the version number.
 * Again, the meta may already have been removed if the update of the version number
 * previously failed.
 */
