#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include "scambio/header.h"
#include "misc.h"
#include "main.h"
#include "scambio.h"
#include "daemon.h"
#include "auth.h"

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
	if (smdir->conf) header_del(smdir->conf);
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
}

/*
 * MDIR thread
 */

static void process_put(struct mdir *mdir, struct header *h, mdir_version version, void *data)
{
	struct strib_mdir *smdir = DOWNCAST(mdir, mdir, strib_mdir);
	/* FIXME: we will not see transient msg again, so we can not reliably process locally generated
	 *        messages. To fix this, we could add a flag to mdir_path_list() to list only synched msgs.
	 */
	if (version < 0) return;

	debug("Stributing message %"PRIversion, version);
	(void)h;
	(void)data;
	smdir->last_done_version = version;
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
