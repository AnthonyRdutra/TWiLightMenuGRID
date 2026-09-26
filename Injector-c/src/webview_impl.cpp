/* Single translation unit that instantiates the vendored webview library's implementation
 * (webview.h is declarations-only in plain C -- see vendor/webview.h; its implementation is C++
 * and picks a platform backend automatically: Cocoa/WebKit on Apple, GTK/WebKitGTK on other
 * Unix, WebView2 on Windows). Mirrors src/vendor_impl.c's role for the other vendored
 * header-only libraries, just as its own .cpp file since this one needs a C++ compiler. */
#include "webview.h"

/* platform_pick_file() (ui/platform_dialog.h) lives here too, one implementation per backend,
 * since webview.h has already pulled in exactly the platform UI headers (Cocoa/GTK/Win32) each
 * one needs by the time this file finishes including it above -- see WEBVIEW_COCOA/GTK/EDGE
 * below, set by webview.h itself based on the current platform. */
#include "ui/platform_dialog.h"

#if defined(WEBVIEW_COCOA)

// NSOpenPanel via the same webview::detail::objc::msg_send() runtime-messaging helper
// vendor/webview.h itself uses for its own file-upload panel (see
// create_webkit_ui_delegate()'s "runOpenPanelWithParameters..." handler above) -- no Objective-C++
// (.mm) compilation needed, same as the rest of this library.
extern "C" int platform_pick_file(const char *title, char *out_path, size_t out_size) {
  using namespace webview::detail;
  using namespace webview::detail::objc;
  autoreleasepool pool;

  id panel = msg_send<id>("NSOpenPanel"_cls, "openPanel"_sel);
  msg_send<void>(panel, "setCanChooseFiles:"_sel, YES);
  msg_send<void>(panel, "setCanChooseDirectories:"_sel, NO);
  msg_send<void>(panel, "setAllowsMultipleSelection:"_sel, NO);
  if (title && title[0]) {
    id title_str = msg_send<id>("NSString"_cls, "stringWithUTF8String:"_sel, title);
    msg_send<void>(panel, "setTitle:"_sel, title_str);
  }

  auto response = msg_send<NSModalResponse>(panel, "runModal"_sel);
  if (response != NSModalResponseOK) return 0;

  id url = msg_send<id>(panel, "URL"_sel);
  id path_str = msg_send<id>(url, "path"_sel);
  const char *path = msg_send<const char *>(path_str, "UTF8String"_sel);
  if (!path || !path[0]) return 0;
  snprintf(out_path, out_size, "%s", path);
  return 1;
}

#elif defined(WEBVIEW_GTK)

extern "C" int platform_pick_file(const char *title, char *out_path, size_t out_size) {
  GtkWidget *dialog = gtk_file_chooser_dialog_new(
      title && title[0] ? title : "Select a file", NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

  int ok = 0;
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if (path) {
      snprintf(out_path, out_size, "%s", path);
      g_free(path);
      ok = 1;
    }
  }
  gtk_widget_destroy(dialog);
  /* Let GTK actually tear the dialog window down before returning control to the webview's own
   * event loop, same as it would between any other two GTK-driven UI actions. */
  while (gtk_events_pending()) gtk_main_iteration();
  return ok;
}

#elif defined(WEBVIEW_EDGE)

#include <windows.h>
#include <commdlg.h>
#include <string>

extern "C" int platform_pick_file(const char *title, char *out_path, size_t out_size) {
  wchar_t file[MAX_PATH] = L"";

  std::wstring wtitle;
  if (title && title[0]) {
    int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    wtitle.resize(len > 0 ? (size_t)len : 0);
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, title, -1, &wtitle[0], len);
  }

  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  ofn.lpstrTitle = wtitle.empty() ? NULL : wtitle.c_str();

  if (!GetOpenFileNameW(&ofn)) return 0;

  int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, out_path, (int)out_size, NULL, NULL);
  return n > 0;
}

#else

// Unreachable in practice -- webview.h (included above) always defines exactly one of
// WEBVIEW_COCOA/GTK/EDGE for the three platforms this project targets. Matches appdir.c's own
// "no idea what this platform is" fallback (return failure) rather than leaving this undefined
// and turning that into a link error instead of a clean, callable no-op.
extern "C" int platform_pick_file(const char *title, char *out_path, size_t out_size) {
  (void)title;
  (void)out_path;
  (void)out_size;
  return 0;
}

#endif
