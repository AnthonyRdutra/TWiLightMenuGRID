/* Cross-platform "directory the running executable lives in" -- the C equivalent of deploy.py's
 * HERE = os.path.dirname(os.path.abspath(__file__)), used the same way: bundled theme folders,
 * build/dsimenu.srldr, etc. are all found relative to this. */
#ifndef INJECTOR_APPDIR_H
#define INJECTOR_APPDIR_H

#include <stddef.h>

/* Writes the absolute path of the directory containing the running executable into `out`
 * (truncated to `out_size`, always NUL-terminated, no trailing slash). Returns 0 on success, -1
 * if the platform API failed (out is left as an empty string in that case -- callers should
 * fall back to the current working directory). */
int app_dir(char *out, size_t out_size);

#endif
