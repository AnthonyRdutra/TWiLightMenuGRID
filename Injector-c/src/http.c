#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int http_global_init(void) { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1; }

void http_global_cleanup(void) { curl_global_cleanup(); }

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuffer *buf = (HttpBuffer *)userdata;
    size_t add = size * nmemb;
    unsigned char *grown = (unsigned char *)realloc(buf->data, buf->size + add + 1);
    if (!grown) return 0; /* signals an error to libcurl, aborting the transfer */
    buf->data = grown;
    memcpy(buf->data + buf->size, ptr, add);
    buf->size += add;
    buf->data[buf->size] = '\0';
    return add;
}

typedef struct {
    CURL *curl;
    HttpProgressFn on_progress;
    void *user_data;
} ProgressCtx;

/* libcurl's xferinfo signature (CURLOPT_XFERINFOFUNCTION); ul* is always 0 for a GET, so only the
 * download side is forwarded. Returning non-zero would abort the transfer -- never done here. */
static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                        curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    ProgressCtx *ctx = (ProgressCtx *)clientp;
    curl_off_t speed = 0;
    curl_easy_getinfo(ctx->curl, CURLINFO_SPEED_DOWNLOAD_T, &speed);
    ctx->on_progress((size_t)dlnow, (size_t)dltotal, (double)speed, ctx->user_data);
    return 0;
}

int http_get(const char *url, HttpBuffer *out, HttpProgressFn on_progress, void *user_data,
             char *err, size_t err_size) {
    memset(out, 0, sizeof(*out));
    if (err && err_size) err[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (err) snprintf(err, err_size, "curl_easy_init failed");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "TWiLightMenuGRID-Injector/1.0");
    /* Certificate verification is on by default (CURLOPT_SSL_VERIFYPEER/VERIFYHOST) -- leave it
     * that way; this talks to screenscraper.fr over plain internet, not a pinned/self-signed
     * host, so there's no legitimate reason to weaken it here. */

    ProgressCtx pctx;
    if (on_progress) {
        pctx.curl = curl;
        pctx.on_progress = on_progress;
        pctx.user_data = user_data;
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); /* progress meter is off by default */
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pctx);
    }

    char curl_err[CURL_ERROR_SIZE];
    curl_err[0] = '\0';
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        if (err) snprintf(err, err_size, "%s", curl_err[0] ? curl_err : curl_easy_strerror(rc));
        curl_easy_cleanup(curl);
        http_buffer_free(out);
        return -1;
    }

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (status < 200 || status >= 300) {
        if (err) snprintf(err, err_size, "HTTP %ld", status);
        http_buffer_free(out);
        return -1;
    }
    return 0;
}

void http_buffer_free(HttpBuffer *buf) {
    if (!buf) return;
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
}
