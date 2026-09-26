#include "screenscraper.h"
#include "http.h"
#include "cJSON.h"

#include <ctype.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

static const int MIN_ART_SIZE = 256; /* mirrors Skyscraper's own MINARTSIZE sanity check */

static void copy_env(char *dst, size_t dst_size, const char *name) {
    const char *v = getenv(name);
    snprintf(dst, dst_size, "%s", v ? v : "");
}

int screenscraper_creds_from_env(ScreenScraperCreds *out) {
    memset(out, 0, sizeof(*out));
    copy_env(out->devid, sizeof(out->devid), "INJECTOR_SS_DEVID");
    copy_env(out->devpassword, sizeof(out->devpassword), "INJECTOR_SS_DEVPASSWORD");
    copy_env(out->ssid, sizeof(out->ssid), "SS_USER");
    copy_env(out->sspassword, sizeof(out->sspassword), "SS_PASS");
    return (out->devid[0] && out->devpassword[0]) ? 0 : -1;
}

static void to_upper_copy(char *dst, size_t dst_size, const char *src) {
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    size_t i;
    for (i = 0; i < n; i++) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

/* curl_easy_escape needs a handle but doesn't otherwise use its connection/transfer state, so a
 * short-lived throwaway one (never used for a request) is fine here. */
static void url_encode(const char *in, char *out, size_t out_size) {
    CURL *tmp = curl_easy_init();
    if (!tmp) {
        snprintf(out, out_size, "%s", in);
        return;
    }
    char *enc = curl_easy_escape(tmp, in, 0);
    snprintf(out, out_size, "%s", enc ? enc : in);
    if (enc) curl_free(enc);
    curl_easy_cleanup(tmp);
}

/* Region/type priority for picking ONE media entry out of jeu.medias[] -- mirrors the shape of
 * Skyscraper's getJsonText(REGION, {"wheel","wheel-hd"}) region-prioritized search, simplified to
 * a fixed order instead of a user-configurable one. */
static const char *REGION_PRIORITY[] = {"wor", "us", "eu", "ss", "jp", NULL};
static const char *WHEEL_TYPES[] = {"wheel-hd", "wheel", NULL};

static int media_matches(cJSON *media, const char *type, const char *region) {
    cJSON *type_j = cJSON_GetObjectItemCaseSensitive(media, "type");
    cJSON *url_j = cJSON_GetObjectItemCaseSensitive(media, "url");
    if (!cJSON_IsString(type_j) || !cJSON_IsString(url_j)) return 0;
    if (strcmp(type_j->valuestring, type) != 0) return 0;
    if (region) {
        cJSON *region_j = cJSON_GetObjectItemCaseSensitive(media, "region");
        if (!cJSON_IsString(region_j) || strcmp(region_j->valuestring, region) != 0) return 0;
    }
    return 1;
}

static int pick_media_url(cJSON *medias, char *out_url, size_t out_size) {
    if (!cJSON_IsArray(medias)) return 0;

    for (int r = 0; REGION_PRIORITY[r]; r++) {
        for (int t = 0; WHEEL_TYPES[t]; t++) {
            cJSON *media;
            cJSON_ArrayForEach(media, medias) {
                if (media_matches(media, WHEEL_TYPES[t], REGION_PRIORITY[r])) {
                    cJSON *url_j = cJSON_GetObjectItemCaseSensitive(media, "url");
                    snprintf(out_url, out_size, "%s", url_j->valuestring);
                    return 1;
                }
            }
        }
    }
    /* Fallback: any region, as long as the type matches -- some games only ship region-less or
     * odd-region art. */
    for (int t = 0; WHEEL_TYPES[t]; t++) {
        cJSON *media;
        cJSON_ArrayForEach(media, medias) {
            if (media_matches(media, WHEEL_TYPES[t], NULL)) {
                cJSON *url_j = cJSON_GetObjectItemCaseSensitive(media, "url");
                snprintf(out_url, out_size, "%s", url_j->valuestring);
                return 1;
            }
        }
    }
    return 0;
}

void screenscraper_parse_response(const char *response_text, ScreenScraperLookup *out) {
    memset(out, 0, sizeof(*out));

    if (!response_text || !response_text[0]) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "empty response from the API");
        return;
    }

    /* ScreenScraper sometimes answers with a plain-text message instead of JSON for these
     * conditions -- checked before attempting to parse, same order/reasoning as
     * ScreenScraper::getSearchResults() in Skyscraper's own source. */
    if (strstr(response_text, "non trouv")) { /* "...non trouvée" */
        out->status = SCREENSCRAPER_NOT_FOUND;
        return;
    }
    if (strstr(response_text, "API totalement ferm")) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "The ScreenScraper API is currently closed.");
        return;
    }
    if (strstr(response_text, "Votre quota de scrape est")) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "ScreenScraper's daily quota has been reached.");
        return;
    }
    if (strstr(response_text, "API fermé pour les non membres") ||
        strstr(response_text, "API closed for non-registered members")) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message),
                 "API closed for anonymous users -- set SS_USER/SS_PASS with your ScreenScraper account.");
        return;
    }

    cJSON *root = cJSON_Parse(response_text);
    if (!root) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "Invalid (JSON) response from the API.");
        return;
    }

    cJSON *header = cJSON_GetObjectItemCaseSensitive(root, "header");
    cJSON *success = header ? cJSON_GetObjectItemCaseSensitive(header, "success") : NULL;
    if (!cJSON_IsString(success) || strcmp(success->valuestring, "true") != 0) {
        cJSON *error = header ? cJSON_GetObjectItemCaseSensitive(header, "error") : NULL;
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "%s",
                 cJSON_IsString(error) ? error->valuestring : "Request was not successful.");
        cJSON_Delete(root);
        return;
    }

    cJSON *response = cJSON_GetObjectItemCaseSensitive(root, "response");
    cJSON *jeu = response ? cJSON_GetObjectItemCaseSensitive(response, "jeu") : NULL;
    if (!cJSON_IsObject(jeu)) {
        out->status = SCREENSCRAPER_NOT_FOUND;
        cJSON_Delete(root);
        return;
    }

    cJSON *medias = cJSON_GetObjectItemCaseSensitive(jeu, "medias");
    pick_media_url(medias, out->wheel_url, sizeof(out->wheel_url)); /* leaves "" if none found */
    out->status = SCREENSCRAPER_OK;
    cJSON_Delete(root);
}

void screenscraper_lookup_game(const ScreenScraperCreds *creds, const RomHash *hash,
                                long long rom_size, const char *rom_filename,
                                ScreenScraperLookup *out) {
    memset(out, 0, sizeof(*out));

    char enc_devid[192], enc_devpass[192], enc_ssid[192], enc_sspass[192], enc_filename[1200];
    url_encode(creds->devid, enc_devid, sizeof(enc_devid));
    url_encode(creds->devpassword, enc_devpass, sizeof(enc_devpass));
    url_encode(creds->ssid, enc_ssid, sizeof(enc_ssid));
    url_encode(creds->sspassword, enc_sspass, sizeof(enc_sspass));
    url_encode(rom_filename, enc_filename, sizeof(enc_filename));

    char crc_upper[9], md5_upper[33], sha1_upper[41];
    to_upper_copy(crc_upper, sizeof(crc_upper), hash->crc32);
    to_upper_copy(md5_upper, sizeof(md5_upper), hash->md5);
    to_upper_copy(sha1_upper, sizeof(sha1_upper), hash->sha1);

    char url[2200];
    int n = snprintf(url, sizeof(url),
        "https://www.screenscraper.fr/api2/jeuInfos.php"
        "?devid=%s&devpassword=%s&softname=twilightmenugrid-injector&output=json&systemeid=%s"
        "%s%s%s%s"
        "&crc=%s&md5=%s&sha1=%s&romnom=%s&romtaille=%lld",
        enc_devid, enc_devpass, SCREENSCRAPER_NDS_SYSTEME_ID,
        creds->ssid[0] ? "&ssid=" : "", creds->ssid[0] ? enc_ssid : "",
        creds->sspassword[0] ? "&sspassword=" : "", creds->sspassword[0] ? enc_sspass : "",
        crc_upper, md5_upper, sha1_upper, enc_filename, rom_size);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "Lookup URL is too long.");
        return;
    }

    HttpBuffer buf;
    char http_err[256];
    if (http_get(url, &buf, NULL, NULL, http_err, sizeof(http_err)) != 0) {
        out->status = SCREENSCRAPER_ERROR;
        snprintf(out->message, sizeof(out->message), "%s", http_err);
        return;
    }

    screenscraper_parse_response((const char *)buf.data, out);
    http_buffer_free(&buf);
}

void screenscraper_parse_login_response(const char *response_text, char *out_message,
                                         size_t out_message_size) {
    out_message[0] = '\0';

    if (!response_text || !response_text[0]) {
        snprintf(out_message, out_message_size, "Empty response from the API.");
        return;
    }
    if (strstr(response_text, "API totalement ferm")) {
        snprintf(out_message, out_message_size, "The ScreenScraper API is currently closed.");
        return;
    }
    if (strstr(response_text, "Votre quota de scrape est")) {
        snprintf(out_message, out_message_size, "ScreenScraper's daily quota has been reached.");
        return;
    }

    cJSON *root = cJSON_Parse(response_text);
    if (!root) {
        snprintf(out_message, out_message_size, "Invalid (JSON) response from the API.");
        return;
    }

    cJSON *header = cJSON_GetObjectItemCaseSensitive(root, "header");
    cJSON *success = header ? cJSON_GetObjectItemCaseSensitive(header, "success") : NULL;
    if (!cJSON_IsString(success) || strcmp(success->valuestring, "true") != 0) {
        cJSON *error = header ? cJSON_GetObjectItemCaseSensitive(header, "error") : NULL;
        snprintf(out_message, out_message_size, "%s",
                 cJSON_IsString(error) ? error->valuestring : "Invalid username or password.");
        cJSON_Delete(root);
        return;
    }

    cJSON *response = cJSON_GetObjectItemCaseSensitive(root, "response");
    cJSON *ssuser = response ? cJSON_GetObjectItemCaseSensitive(response, "ssuser") : NULL;
    if (!cJSON_IsObject(ssuser)) {
        snprintf(out_message, out_message_size, "Invalid username or password.");
    }
    cJSON_Delete(root);
}

int screenscraper_validate_login(const ScreenScraperCreds *creds, char *out_message,
                                  size_t out_message_size) {
    out_message[0] = '\0';

    char enc_devid[192], enc_devpass[192], enc_ssid[192], enc_sspass[192];
    url_encode(creds->devid, enc_devid, sizeof(enc_devid));
    url_encode(creds->devpassword, enc_devpass, sizeof(enc_devpass));
    url_encode(creds->ssid, enc_ssid, sizeof(enc_ssid));
    url_encode(creds->sspassword, enc_sspass, sizeof(enc_sspass));

    char url[1200];
    int n = snprintf(url, sizeof(url),
        "https://www.screenscraper.fr/api2/ssuserInfos.php"
        "?devid=%s&devpassword=%s&softname=twilightmenugrid-injector&output=json"
        "&ssid=%s&sspassword=%s",
        enc_devid, enc_devpass, enc_ssid, enc_sspass);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        snprintf(out_message, out_message_size, "Validation URL is too long.");
        return -1;
    }

    HttpBuffer buf;
    char http_err[256];
    if (http_get(url, &buf, NULL, NULL, http_err, sizeof(http_err)) != 0) {
        snprintf(out_message, out_message_size, "%s", http_err);
        return -1;
    }

    screenscraper_parse_login_response((const char *)buf.data, out_message, out_message_size);
    http_buffer_free(&buf);
    return out_message[0] ? -1 : 0;
}

int screenscraper_download_logo(const char *wheel_url, HttpProgressFn on_progress, void *user_data,
                                 unsigned char **out_data, size_t *out_size,
                                 char *err, size_t err_size) {
    HttpBuffer buf;
    if (http_get(wheel_url, &buf, on_progress, user_data, err, err_size) != 0) return -1;
    if (buf.size < (size_t)MIN_ART_SIZE) {
        if (err) snprintf(err, err_size, "Image response is too small (%zu bytes).", buf.size);
        http_buffer_free(&buf);
        return -1;
    }
    *out_data = buf.data;
    *out_size = buf.size;
    return 0;
}

/* --- end-user login cache ----------------------------------------------------------------- */

static void login_cache_path(const char *app_dir, char *out, size_t out_size) {
    size_t blen = strlen(app_dir);
    while (blen > 0 && (app_dir[blen - 1] == '/' || app_dir[blen - 1] == '\\')) blen--;
    snprintf(out, out_size, "%.*s/%s", (int)blen, app_dir, SCREENSCRAPER_LOGIN_CACHE_FILENAME);
}

int screenscraper_login_cache_load(const char *app_dir, char *out_user, size_t out_user_size,
                                    char *out_pass, size_t out_pass_size, int *out_has_pass) {
    out_user[0] = '\0';
    if (out_pass) out_pass[0] = '\0';
    if (out_has_pass) *out_has_pass = 0;

    char path[1200];
    login_cache_path(app_dir, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return -1;
    }
    char *text = (char *)malloc((size_t)sz + 1);
    size_t n = fread(text, 1, (size_t)sz, f);
    text[n] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) return -1;

    cJSON *user_j = cJSON_GetObjectItemCaseSensitive(root, "username");
    cJSON *pass_j = cJSON_GetObjectItemCaseSensitive(root, "password");
    int found = 0;
    if (cJSON_IsString(user_j) && user_j->valuestring[0]) {
        snprintf(out_user, out_user_size, "%s", user_j->valuestring);
        found = 1;
        if (cJSON_IsString(pass_j) && pass_j->valuestring[0]) {
            if (out_pass) snprintf(out_pass, out_pass_size, "%s", pass_j->valuestring);
            if (out_has_pass) *out_has_pass = 1;
        }
    }
    cJSON_Delete(root);
    return found ? 0 : -1;
}

int screenscraper_login_cache_save(const char *app_dir, const char *user, const char *pass) {
    char path[1200];
    login_cache_path(app_dir, path, sizeof(path));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", user);
    cJSON_AddStringToObject(root, "password", pass);
    char *text = cJSON_Print(root);
    cJSON_Delete(root);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(text);
        return -1;
    }
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    fclose(f);
    free(text);

#if !defined(_WIN32)
    chmod(path, 0600); /* best-effort -- same intent as fetch_ds_media.py's own os.chmod(0o600) */
#endif
    return written == len ? 0 : -1;
}

void screenscraper_login_cache_clear(const char *app_dir) {
    char path[1200];
    login_cache_path(app_dir, path, sizeof(path));
    remove(path);
}
