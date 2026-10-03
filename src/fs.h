/*
 * fs.h - filesystem helpers.
 */
#ifndef FS_H
#define FS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Write the folder containing the running exe into out (no trailing '\').
 * Returns false if the path does not fit.
 */
bool fs_exe_dir(char *out, size_t cap);

/* True if path names an existing file or folder. */
bool fs_exists(const char *path);

/*
 * Create every parent folder of path, like "mkdir -p" on its folder part.
 * The file name after the last '\' is ignored. Existing folders are fine.
 */
void fs_make_parent_dirs(const char *path);

/*
 * Recursively delete path and everything in it. A file that can't be deleted
 * (e.g. a spawned exe still running) is skipped and counted in *failed; every
 * other file is still removed and counted in *deleted. Folders that end up
 * empty are removed; folders still holding a locked file remain. Junctions and
 * symlinks are removed as links and never followed.
 */
void fs_delete_tree(const char *path, int *deleted, int *failed);

#endif /* FS_H */
