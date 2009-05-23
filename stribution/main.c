#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include "scambio/header.h"
#include "misc.h"
#include "main.h"
#include "scambio.h"
#include "daemon.h"
#include "auth.h"
#include "stribution.h"

/* For each mdir, we have a secret file storing the last known
 * stribution config with its version, and another one telling us the last
 * version that was processed (this is important because stribution action is
 * not necessarily a projection). Then we read a cursor from this version, and
 * for each message :
 *
 * - if it's a stribution config message newer than the stored one, update the
 * stored conf with it (and optionaly reset the last processed version and
 * restart this folder.
 *
 * - otherwise, apply the config to the message, which will give us a list of
 * mdir to copy it, and a flag telling us weither to delete it from there or
 * not. Perform the copies and the delete, but remember this result for the
 * associated marks.
 *
 * - if the message is a mark, retrieve the referred message (even if deleted
 * recently), and use this one as the filter subject. Then move the tag. If the
 * referenced message cannot be retrieved, delete the mark as well.
 *
 */

/*
 * Stribution Mdir
 */

static struct mdir *smdir_alloc(char const *path)
{
	struct strib_mdir *smdir = Malloc(sizeof(*smdir));
	smdir->conf = NULL;
	smdir->thread = NULL;
	smdir->stribution = NULL;

	// First, read the secret header file
	char filename[PATH_MAX];
	snprintf(filename, sizeof(filename), "%s/"STRIB_SECRET_FILE, path);
	if_fail (smdir->conf = header_from_file(filename)) {
		if (! error_code() == ENOENT) goto fail;
		error_clear();
		// Initialize an empty conf
		smdir->conf = header_new();
	}

	// Extract some fields from the header for easier processing
	smdir->last_conf_version = smdir->last_done_version = 0;
	struct header_field *hf;
	if (NULL != (hf = header_find(smdir->conf, STRIB_LAST_CONF_VERSION, NULL))) {
		if_fail (smdir->last_conf_version = mdir_str2version(hf->value)) goto fail;
	}
	if (NULL != (hf = header_find(smdir->conf, STRIB_LAST_DONE_VERSION, NULL))) {
		if_fail (smdir->last_done_version = mdir_str2version(hf->value)) goto fail;
	}

	return &smdir->mdir;

fail:
	if (smdir->conf) header_unref(smdir->conf);
	free(smdir);
	return NULL;
}

static void remove_all_field(struct header *h, char const *name)
{
	struct header_field *hf;
	while (NULL != (hf = header_find(h, name, NULL))) {
		header_field_del(hf, h);
	}
}

static void replace_header_field(struct header *h, char const *name, char const *value)
{
	remove_all_field(h, name);
	(void)header_field_new(h, name, value);
}

static void smdir_free(struct mdir *mdir)
{
	struct strib_mdir *smdir = DOWNCAST(mdir, mdir, strib_mdir);

	// Update the conf with last versions processed
	replace_header_field(smdir->conf, STRIB_LAST_CONF_VERSION, mdir_version2str(smdir->last_conf_version));
	replace_header_field(smdir->conf, STRIB_LAST_DONE_VERSION, mdir_version2str(smdir->last_done_version));

	// Save the result in secret file
	char filename[PATH_MAX];
	snprintf(filename, sizeof(filename), "%s/"STRIB_SECRET_FILE, smdir->mdir.path);
	if_fail (header_to_file(smdir->conf, filename)) {
		error_clear();	// show must go on
	}

	// Free the stribution
	if (smdir->stribution) {
		strib_del(smdir->stribution);
		smdir->stribution = NULL;
	}

	// Actual free
	free(smdir);
}

/*
 * MDIR thread
 */

struct action_param {
	struct mdir *mdir;
	mdir_version version;
	struct header *object, *subject;
};

static void do_delete(struct action_param const *param)
{
	mdir_del_request(param->mdir, param->version);
}

static void create_folder(struct mdir *parent, char const *relname)
{
	struct header *h = header_new();
	(void)header_field_new(h, SC_TYPE_FIELD, SC_DIR_TYPE);
	(void)header_field_new(h, SC_NAME_FIELD, relname);
	if_fail (mdir_patch_request(parent, MDIR_ADD, h)) return;
}

static void do_copy_string(struct action_param const *param, char const *relname)
{
	// Name is relative to parent
	char absname[PATH_MAX];
	snprintf(absname, sizeof(absname), "%s/%s", mdir_name(param->mdir), relname);

	struct mdir *destdir = mdir_lookup(absname);
	on_error {	// create it then
		error_clear();
		if_fail (create_folder(param->mdir, relname)) return;
		if_fail (destdir = mdir_lookup(absname)) return;
	}

	mdir_patch_request(destdir, MDIR_ADD, param->object);
}

static void do_copy_deref(struct action_param const *param, struct strib_deref const *deref)
{
	// We take the subfolder's name from on of the subject header fields
	struct header_field *hf = header_find(param->subject, deref->name, NULL);
	if (! hf) {
		with_error(0, "Should use subfolder name from message field '%s' which is unset!", deref->name)
			return;
	}
	do_copy_string(param, hf->value);
}

static void do_copy(struct action_param const *param, struct strib_action const *action)
{
	switch (action->dest_type) {
		case DEST_STRING: do_copy_string(param, action->dest.string); break;
		case DEST_DEREF:  do_copy_deref(param, &action->dest.deref); break;
		default:          assert(0);
	}
}

static void action_cb(struct header const *subject, struct strib_action const *action, void *data)
{
	(void)subject;
	struct action_param const *param = data;

	switch (action->type) {
		case ACTION_DELETE: do_delete(param); break;
		case ACTION_COPY:   do_copy(param, action); break;
		case ACTION_MOVE:   if_succeed (do_copy(param, action)) { do_delete(param); } break;
		default:            assert(0);
	}
}

static void process_message(struct strib_mdir *smdir, struct header *subject, struct header *object, mdir_version version)
{
	assert(smdir->conf);
	if (! smdir->stribution) return;

	struct action_param param = {
		.mdir = &smdir->mdir,
		.version = version,
		.object = object,
		.subject = subject
	};
	strib_eval(smdir->stribution, subject, action_cb, &param);
}

static void process_put(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	(void)data;
	struct strib_mdir *smdir = DOWNCAST(mdir, mdir, strib_mdir);
	/* FIXME: we will not see transient msg again, so we can not reliably process locally generated
	 *        messages. To fix this, we could add a flag to mdir_path_list() to list only synched msgs.
	 */
	if (version < 0) return;

	debug("Stributing message %"PRIversion, version);
	smdir->last_done_version = version;

	smdir->last_done_version = version;
	smdir->last_done_version = version;
	struct header *h_alternate = NULL;	// the header to use as subject to conf
	// Find out message's type
	struct header_field *hf_type = header_find(h, SC_TYPE_FIELD, NULL);
	if (hf_type) {
		if (0 == strcmp(hf_type->value, SC_DIR_TYPE)) return;	// Skip directories
		if (0 == strcmp(hf_type->value, SC_STRIB_TYPE)) {
			// For now we merely change the last_conf. Later, optionaly reset the cursor.
			struct stribution *strib = strib_new(h);
			on_error {	// just ignore this msg
				return;
			}
			smdir->last_conf_version = version;
			header_unref(smdir->conf);
			smdir->conf = header_ref(h);
			if (smdir->stribution) strib_del(smdir->stribution);
			smdir->stribution = strib;
			return;
		}
		if (0 == strcmp(hf_type->value, SC_MARK_TYPE)) {
			if_fail (h_alternate = mdir_get_targeted_header(mdir, h)) return;	// FIXME: change this to return even deleted messages
		}
	}
	
	// Process h with smdir->stribution (if any), using h_alternate if set
	process_message(smdir, h_alternate ? h_alternate : h, h, version);

	if (h_alternate) header_unref(h_alternate);
}

static void *smdir_thread(void *arg)
{
	struct strib_mdir *smdir = arg;
	mdir_cursor_ctor(&smdir->cursor);
	mdir_cursor_seek(&smdir->cursor, smdir->last_done_version);
	while (! is_error()) {
		mdir_patch_list(&smdir->mdir, &smdir->cursor, false, process_put, NULL, NULL, NULL);
	}
	error_clear();
	return NULL;
}

static void smdir_start_thread(struct strib_mdir *smdir)
{
	assert(! smdir->thread);
	smdir->thread = pth_spawn(PTH_ATTR_DEFAULT, smdir_thread, smdir);
	// we don't care of failure since it will automaticaly be retried later.
}

/*
 * Walk through folders
 */

static void start_dir_thread_rec(struct mdir *parent, struct mdir *mdir, bool new, char const *name, void *data)
{
	(void)parent;
	(void)new;
	(void)data;
	debug("Walking through folder %s (%s)", name, mdir->path);

	// Start a thread on this directory
	struct strib_mdir *smdir = DOWNCAST(mdir, mdir, strib_mdir);
	if (! smdir->thread) {
		smdir_start_thread(smdir);
	}

	// Recurse
	mdir_folder_list(mdir, false, start_dir_thread_rec, NULL);
}

/*
 * Init
 */

static void init_conf(void)
{
	conf_set_default_str("SC_LOG_DIR", "/var/log/scambio");
	conf_set_default_int("SC_LOG_LEVEL", 3);
	conf_set_default_str("STRIB_ROOT", "root");
}

static void init_log(void)
{
	if_fail (log_begin(conf_get_str("SC_LOG_DIR"), "stribution")) return;
	if (0 != atexit(log_end)) with_error(0, "atexit") return;
	log_level = conf_get_int("SC_LOG_LEVEL");
	debug("Seting log level to %d", log_level);
}

static void init(void)
{
	if_fail (init_conf()) return;
	if_fail (init_log()) return;
	if_fail (daemonize("sc_stribution")) return;
	if_fail (mdir_init()) return;
	if_fail (auth_init()) return;
	mdir_alloc = smdir_alloc;
	mdir_free = smdir_free;
}

int main(void)
{
	if (! pth_init()) return EXIT_FAILURE;
	error_begin();
	if (0 != atexit(error_end)) return EXIT_FAILURE;
	if_fail (init()) return EXIT_FAILURE;

	char const *root_name = conf_get_str("STRIB_ROOT");
	struct mdir *root = mdir_lookup(root_name);
	on_error return EXIT_FAILURE;

	// FIXME: to discover new folders, do this from time to time
	if_fail (start_dir_thread_rec(NULL, root, false, root_name, NULL)) {
		return EXIT_FAILURE;
	}

	pth_exit(NULL);
	return EXIT_SUCCESS;
}
