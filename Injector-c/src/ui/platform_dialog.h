/* A single native "Open File" dialog, implemented once per platform backend in
 * webview_impl.cpp (same translation unit that already pulls in each platform's UI headers to
 * build vendor/webview.h's own implementation -- see that file's top comment). Kept out of
 * bridge.c/wizard.c on purpose: neither has any business knowing about NSOpenPanel/GTK/Win32. */
#ifndef INJECTOR_UI_PLATFORM_DIALOG_H
#define INJECTOR_UI_PLATFORM_DIALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shows a native, blocking "Open File" dialog titled `title` (may be NULL/empty for a platform
 * default). On success, writes the chosen file's absolute path into `out_path` (truncated to fit
 * `out_size`) and returns 1. Returns 0 if the user cancelled or the dialog couldn't be shown --
 * `out_path` is left untouched either way in that case. Must be called from the UI thread (true
 * of every webview_bind callback already, see bridge.c). */
int platform_pick_file(const char *title, char *out_path, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
