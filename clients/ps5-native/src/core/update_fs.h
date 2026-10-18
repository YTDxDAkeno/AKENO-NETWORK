/*
 * File helpers of the updater: atomic writes (temporary file, fsync, rename), bounded reads and a
 * bounded recursive delete that never follows symbolic links. Every path the updater touches is
 * below the launcher folder's "update" directory (update_install.h).
 */
#ifndef AKN_UPDATE_FS_H
#define AKN_UPDATE_FS_H

#include <stddef.h>

/* Writes path atomically: <path>.tmp, fsync, rename. 0, or -1 with errno set. */
int akn_fs_write_atomic(const char *path, const void *data, size_t len);

/* Reads a whole regular file of at most max bytes (NUL-terminated; free()). NULL on failure. */
char *akn_fs_read_file(const char *path, size_t max, size_t *len);

/* mkdir -p for dir (mode 0755). 0, or -1. */
int akn_fs_mkdirs(const char *dir);

/* Deletes dir and everything below it (depth-limited, symbolic links removed, never followed).
 * A missing dir is success. 0, or -1. */
int akn_fs_remove_tree(const char *dir);

int akn_fs_is_dir(const char *path);
int akn_fs_is_file(const char *path);

/* fsync of a directory, so a rename inside it is durable (best effort). */
void akn_fs_sync_dir(const char *dir);

/* Free bytes on the file system holding path, or -1 when unknown. */
long long akn_fs_free_bytes(const char *path);

/* "No space left on the console's storage" etc. for errno values the user can act on. */
const char *akn_fs_error_text(int err);

#endif
