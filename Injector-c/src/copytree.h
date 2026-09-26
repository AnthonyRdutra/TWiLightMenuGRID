/* Recursive merge-copy, ported 1:1 from deploy.py's merge_copy_tree(). */
#ifndef INJECTOR_COPYTREE_H
#define INJECTOR_COPYTREE_H

/* Creates every path component of `dir`, tolerating components that already exist -- like
 * Python's os.makedirs(exist_ok=True). Returns 0 on success, -1 on a hard I/O error. */
int fs_makedirs(const char *dir);

/* Copies a single file's contents and mtime (like Python's shutil.copy2). Returns 0 on success,
 * -1 on I/O error. Exposed for one-off copies outside a tree walk (srldr install, settings.ini
 * backups) that don't need copytree_merge()'s recursion/skip-list. */
int copytree_copy_file(const char *src, const char *dst);

/* Recursively copies every file under `src` into the same relative location under `dst`,
 * creating directories as needed. Skips ".DS_Store" and any filename starting with "._"
 * (AppleDouble sidecar files). Preserves each file's mtime (like Python's shutil.copy2).
 * Never deletes or otherwise touches anything already in `dst` that isn't also being copied
 * from `src` -- this is a merge, not a mirror.
 * Returns 0 on success, -1 on the first hard I/O error encountered. */
int copytree_merge(const char *src, const char *dst);

#endif
