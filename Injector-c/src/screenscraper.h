/* Our own ScreenScraper.fr API client (jeuInfos.php), replacing the Skyscraper CLI the Python
 * tools shell out to. Ported from studying Skyscraper's own src/screenscraper.cpp (request
 * shape, error-string checks, media-priority logic) -- NOT from reusing its code or credentials.
 *
 * IMPORTANT: ScreenScraper requires per-APPLICATION "devid"/"devpassword" credentials (distinct
 * from a user's own login). Skyscraper bakes its own (obfuscated) devid/devpassword into its
 * binary; those are registered to Skyscraper specifically and reusing them here would mean
 * impersonating another registered application, which this project won't do. Register a free
 * app at https://www.screenscraper.fr (Contribuer > Developpement) and set
 * INJECTOR_SS_DEVID / INJECTOR_SS_DEVPASSWORD in the environment -- see
 * screenscraper_creds_from_env(). End users still get their own optional ssid/sspassword login
 * (SS_USER/SS_PASS, same env vars fetch_ds_media.py already used) for a higher request quota. */
#ifndef INJECTOR_SCREENSCRAPER_H
#define INJECTOR_SCREENSCRAPER_H

#include <stddef.h>
#include "romhash.h"
#include "http.h"

#define SCREENSCRAPER_NDS_SYSTEME_ID "15"

typedef struct {
    char devid[64];
    char devpassword[128];
    char ssid[64];        /* optional end-user login; empty if unset */
    char sspassword[128]; /* optional */
} ScreenScraperCreds;

/* Loads devid/devpassword from INJECTOR_SS_DEVID/INJECTOR_SS_DEVPASSWORD (required) and the
 * optional end-user login from SS_USER/SS_PASS. Returns 0 if devid/devpassword are both set, -1
 * otherwise (out is still filled in with whatever WAS found, so an error message can quote it). */
int screenscraper_creds_from_env(ScreenScraperCreds *out);

typedef enum {
    SCREENSCRAPER_OK,        /* request succeeded; wheel_url is set if art exists for this game */
    SCREENSCRAPER_NOT_FOUND, /* game isn't in ScreenScraper's database -- not an error */
    SCREENSCRAPER_ERROR,     /* network/auth/quota/parse error -- message explains */
} ScreenScraperStatus;

typedef struct {
    ScreenScraperStatus status;
    char wheel_url[1024]; /* set when status == SCREENSCRAPER_OK and logo art exists */
    char message[256];    /* human-readable detail, always set on ERROR */
} ScreenScraperLookup;

/* Looks up one ROM by hash (jeuInfos.php) and extracts the best logo ("wheel"/"wheel-hd") media
 * URL for it, preferring region "wor" > "us" > "eu" > "ss" > "jp" (mirrors Skyscraper's own
 * region-priority media lookup, simplified to a fixed order instead of a configurable one).
 * `rom_filename`/`rom_size` ride along as romnom/romtaille the same way Skyscraper's own
 * getSearchNames() sends them -- ScreenScraper uses them as a disambiguation fallback, not as
 * the primary lookup key (crc/md5/sha1 are). */
void screenscraper_lookup_game(const ScreenScraperCreds *creds, const RomHash *hash,
                                long long rom_size, const char *rom_filename,
                                ScreenScraperLookup *out);

/* Downloads the image at `wheel_url` (as returned in a SCREENSCRAPER_OK lookup) into a malloc'd
 * buffer, reporting byte-level progress/speed through `on_progress`/`user_data` (may be NULL) --
 * see HttpProgressFn in http.h. Returns 0 on success (caller frees *out_data with free()), -1 on
 * failure (err set). */
int screenscraper_download_logo(const char *wheel_url, HttpProgressFn on_progress, void *user_data,
                                 unsigned char **out_data, size_t *out_size,
                                 char *err, size_t err_size);

/* --- end-user login cache (ssid/sspassword ONLY -- never devid/devpassword) -------------------- */

/* Same file/shape fetch_ds_media.py's own credential cache already uses ({"username":..,
 * "password":..}, chmod 600 where supported) -- kept identical on purpose so a
 * ".ss_credentials.json" someone already has from running the Python tool can be dropped in
 * directly. Lives at "<app_dir>/.ss_credentials.json". */
#define SCREENSCRAPER_LOGIN_CACHE_FILENAME ".ss_credentials.json"

/* Returns 0 and fills `out_user` if a cached login was found; `out_pass`/`*out_has_pass` are only
 * filled in if a password was cached too (a caller might want to show the saved username without
 * ever pulling the password back into memory needlessly). Returns -1 if no cache file exists. */
int screenscraper_login_cache_load(const char *app_dir, char *out_user, size_t out_user_size,
                                    char *out_pass, size_t out_pass_size, int *out_has_pass);

/* Writes "<app_dir>/.ss_credentials.json" with `user`/`pass` (chmod 600 on POSIX). Returns 0 on
 * success, -1 on I/O error. */
int screenscraper_login_cache_save(const char *app_dir, const char *user, const char *pass);

/* Deletes the cache file, if any -- never an error if it didn't exist. */
void screenscraper_login_cache_clear(const char *app_dir);

/* --- end-user login validation (ssuserInfos.php) ------------------------------------------- */

/* Checks `creds->ssid`/`sspassword` against ScreenScraper's own account API (still requires
 * `creds->devid`/`devpassword` -- the app-level credentials -- since every ScreenScraper request
 * needs them, login check included). Returns 0 if the login was accepted (`out_message` left
 * empty); -1 otherwise, with `out_message` explaining why (wrong password, closed API, network
 * error, ...) so the login screen can show it inline instead of silently proceeding anonymously. */
int screenscraper_validate_login(const ScreenScraperCreds *creds, char *out_message,
                                  size_t out_message_size);

/* --- pure, network-free JSON parsing (split out so it's unit-testable) ------------------------ */

/* Parses a raw jeuInfos.php response body (JSON, or one of the plain-text error bodies
 * ScreenScraper sometimes sends instead) and fills `out` exactly like screenscraper_lookup_game()
 * does after receiving the HTTP response. Exposed directly for tests/test_screenscraper.c --
 * everything in screenscraper_lookup_game() beyond this is just "build the URL, do the GET". */
void screenscraper_parse_response(const char *response_text, ScreenScraperLookup *out);

/* Parses a raw ssuserInfos.php response body the same way -- `out_message` ends up empty on an
 * accepted login, or explains the rejection otherwise. Exposed for tests/test_screenscraper.c;
 * screenscraper_validate_login() beyond this is just "build the URL, do the GET". */
void screenscraper_parse_login_response(const char *response_text, char *out_message,
                                         size_t out_message_size);

#endif
