/*
 *
 * z: scan folders from a root and create a tree structure of folders, aking
 * for sync for each one.
 *
 * a: spawn a thread that apply patches (find ou a physical file name and write
 * the headers into the_folder/.meta/this_filename), increasing this folder's
 * ".version". Or a PATCH may be kept in a per folder cache, waiting for a
 * previous one first (there's an ordered list of struct headers). If the patch
 * is for a directory, handle this locally (add the folder in the tree
 * structure).
 *
 * Those two should be kept apart :
 *
 * b: choose a plugin/type of file for a given header.
 *
 * c: fetch an URL to a given file (given physical name, filder, and file
 * type).
 *
 */
