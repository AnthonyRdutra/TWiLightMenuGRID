/* Minimal HTTPS GET wrapper over libcurl -- the ScreenScraper API client (src/screenscraper.c)
 * is the only caller. Deliberately thin: libcurl already handles TLS correctly (certificate
 * verification stays on; never disable it), so this file has no business reimplementing any of
 * that itself. */
#ifndef INJECTOR_HTTP_H
#define INJECTOR_HTTP_H

#include <stddef.h>

/* Call once before any other http_* function (wraps curl_global_init(), which itself is not
 * thread-safe against other libcurl users -- call this before spawning any worker thread that
 * touches http_get()). http_global_cleanup() undoes it at shutdown. */
int http_global_init(void);
void http_global_cleanup(void);

typedef struct {
    unsigned char *data; /* malloc'd, NOT NUL-terminated-guaranteed for binary payloads, but an
                          * extra NUL is always appended past `size` so text responses can be
                          * treated as a C string directly if the caller wants that. */
    size_t size;
} HttpBuffer;

/* Called repeatedly (roughly a few times a second -- libcurl's own cadence, not something this
 * file controls) while a request is in flight. `bytes_total` is 0 if the server never sent a
 * Content-Length (e.g. chunked transfer) -- callers should treat that as "unknown", not "empty".
 * `speed_bytes_per_sec` is libcurl's own moving-average download speed for this transfer. */
typedef void (*HttpProgressFn)(size_t bytes_done, size_t bytes_total, double speed_bytes_per_sec,
                                void *user_data);

/* Performs an HTTP(S) GET, buffering the whole response body into `out` (caller must release it
 * via http_buffer_free()). `on_progress`/`user_data` may be NULL if the caller doesn't need
 * progress reporting (e.g. the small JSON API calls). Returns 0 on a 2xx response, -1 on a
 * transport error or non-2xx status -- `err` (if non-NULL) gets a human-readable message either
 * way. */
int http_get(const char *url, HttpBuffer *out, HttpProgressFn on_progress, void *user_data,
             char *err, size_t err_size);

void http_buffer_free(HttpBuffer *buf);

#endif
