#include "wizard.h"
#include "sd.h"
#include "ini.h"
#include "copytree.h"
#include "theme_scan.h"
#include "appdir.h"
#include "scrape.h"
#include "screenscraper.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

/* Same subpaths deploy.py hardcodes (DSIMENU_SUBPATH / DSIMENU_ASSETS_SUBPATH /
 * SETTINGS_INI_SUBPATH) -- see the project plan / conversation for where each is read. */
#define DSIMENU_SUBPATH "_nds/TWiLightMenu"
#define DSIMENU_ASSETS_SUBPATH "_nds/TWiLightMenu/dsimenu"
#define SETTINGS_INI_SUBPATH "_nds/TWiLightMenu/settings.ini"

typedef enum {
    STEP_SD_PICK,
    STEP_ALREADY_INSTALLED,
    STEP_THEME_PICK,
    STEP_SCRAPE_ASK,
    STEP_SCRAPE_LOGIN,
    STEP_SCRAPE_LOGIN_CHECKING,
    STEP_SCRAPE_SCANNING,
    STEP_SCRAPE_PREVIEW,
    STEP_SCRAPE_PROGRESS,
    STEP_SCRAPE_DONE,
    STEP_DONE,
} Step;

static const char *step_name(Step s) {
    switch (s) {
        case STEP_SD_PICK: return "sd_pick";
        case STEP_ALREADY_INSTALLED: return "already_installed";
        case STEP_THEME_PICK: return "theme_pick";
        case STEP_SCRAPE_ASK: return "scrape_ask";
        case STEP_SCRAPE_LOGIN: return "scrape_login";
        case STEP_SCRAPE_LOGIN_CHECKING: return "scrape_login_checking";
        case STEP_SCRAPE_SCANNING: return "scrape_scanning";
        case STEP_SCRAPE_PREVIEW: return "scrape_preview";
        case STEP_SCRAPE_PROGRESS: return "scrape_progress";
        case STEP_SCRAPE_DONE: return "scrape_done";
        case STEP_DONE: return "done";
    }
    return "sd_pick";
}

/* Which background job `scrape_thread` (see WizardState below) is currently running, if any.
 * Checking a login, scanning+hashing the SD, and downloading logos never overlap in time, so all
 * three share one thread/mutex/finished flag instead of three copies of the same plumbing. */
typedef enum {
    SCRAPE_JOB_NONE,
    SCRAPE_JOB_LOGIN_CHECK,
    SCRAPE_JOB_SCAN,
    SCRAPE_JOB_DOWNLOAD,
} ScrapeJob;

/* Tiny cross-platform mutex shim -- the only thing src/ui needs from either pthreads or Win32
 * threading, so a full abstraction layer would be overkill. */
#if defined(_WIN32)
typedef CRITICAL_SECTION WizardMutex;
static void wizard_mutex_init(WizardMutex *m) { InitializeCriticalSection(m); }
static void wizard_mutex_lock(WizardMutex *m) { EnterCriticalSection(m); }
static void wizard_mutex_unlock(WizardMutex *m) { LeaveCriticalSection(m); }
static void wizard_mutex_destroy(WizardMutex *m) { DeleteCriticalSection(m); }
#else
typedef pthread_mutex_t WizardMutex;
static void wizard_mutex_init(WizardMutex *m) { pthread_mutex_init(m, NULL); }
static void wizard_mutex_lock(WizardMutex *m) { pthread_mutex_lock(m); }
static void wizard_mutex_unlock(WizardMutex *m) { pthread_mutex_unlock(m); }
static void wizard_mutex_destroy(WizardMutex *m) { pthread_mutex_destroy(m); }
#endif

#if defined(_WIN32)
/* setenv/unsetenv are POSIX; MSVC/MinGW only have _putenv_s. Used to hand SS_USER/SS_PASS to
 * screenscraper_creds_from_env() (see wizard_scrape_login_continue()/_skip()) without changing
 * that function's env-based API on this one platform. */
static void setenv(const char *name, const char *value, int overwrite) {
    (void)overwrite;
    _putenv_s(name, value);
}
static void unsetenv(const char *name) { _putenv_s(name, ""); }
#endif

struct WizardState {
    Step step;

    char app_dir[1024]; /* directory the running executable lives in -- see appdir.h */

    SdCandidates found_cards; /* mounts that already look like a TWiLightMenu SD */
    SdCandidates all_mounts;  /* fallback list when found_cards is empty */

    char chosen_sd[1024];
    char sd_pick_error[256]; /* shown on the sd_pick step when a manual path is rejected */

    BundledThemeList themes;
    int selected_theme; /* index into themes.items, or -1 */

    char log[8192];
    size_t log_len;

    char eject_hint[256];

    /* --------------------------- logo scraping (src/scrape.c) --------------------------- */
    char scrape_assets_dir[1300]; /* <chosen_sd>/DSIMENU_ASSETS_SUBPATH, set entering scrape_ask */
    ScrapePreview scrape_preview;

    /* ScreenScraper end-user login (scrape_login step) -- only the username is ever kept in
     * memory/serialized to the frontend; the cached password stays in the cache file on disk
     * until wizard_scrape_login_continue() reads it back for a request, see screenscraper.c. */
    char scrape_saved_user[128];
    int scrape_has_saved_password;
    char scrape_login_error[256]; /* set once the login-check job (see below) finishes/fails */

    /* Login-check job input -- set by wizard_scrape_login_continue() right before starting the
     * background thread, read once by scrape_login_check_worker_run(). scrape_pending_pass is
     * cleared as soon as the job consumes it (either applied via setenv or discarded). */
    char scrape_pending_user[128];
    char scrape_pending_pass[128];
    int scrape_pending_remember;

    /* Download-job input, set by wizard_scrape_start(). */
    int scrape_force;

    /* Everything below is touched by the background scrape thread (scrape_thread_main(), running
     * one of the SCRAPE_JOB_* workers) as well as the UI thread (wizard_state_json()), guarded by
     * scrape_mutex except where noted. */
    ScrapeJob scrape_job;
    WizardMutex scrape_mutex;
#if defined(_WIN32)
    HANDLE scrape_thread;
#else
    pthread_t scrape_thread;
#endif
    int scrape_thread_started;   /* a join is still owed */
    volatile int scrape_cancel;  /* polled between games/files by scrape_run()/scrape_preview() */
    int scrape_finished;         /* set by the worker; wizard_state_json() polls+joins on this */
    size_t scrape_done, scrape_total;
    char scrape_current_name[512];
    char scrape_current_detail[256];
    /* SCRAPE_JOB_DOWNLOAD only: the current game's own logo download in flight (see
     * scrape_run()'s on_byte_progress) -- item_bytes_total is 0 if unknown (no Content-Length). */
    size_t scrape_item_bytes_done, scrape_item_bytes_total;
    double scrape_item_speed_bps;
    char scrape_job_message[256]; /* SCRAPE_JOB_LOGIN_CHECK's result: empty == accepted */
    ScrapeStats scrape_stats;
    char scrape_run_error[256];
};

static void log_line(WizardState *w, const char *fmt, ...) {
    if (w->log_len + 1 >= sizeof(w->log)) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->log + w->log_len, sizeof(w->log) - w->log_len - 1, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t written = (size_t)n < sizeof(w->log) - w->log_len - 1 ? (size_t)n : sizeof(w->log) - w->log_len - 1;
        w->log_len += written;
    }
    if (w->log_len + 1 < sizeof(w->log)) {
        w->log[w->log_len++] = '\n';
        w->log[w->log_len] = '\0';
    }
}

static void path_join(char *out, size_t out_size, const char *base, const char *rel) {
    size_t blen = strlen(base);
    while (blen > 0 && (base[blen - 1] == '/' || base[blen - 1] == '\\')) blen--;
    snprintf(out, out_size, "%.*s/%s", (int)blen, base, rel);
}

static int path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int path_is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Trims surrounding whitespace and expands a leading "~" (whole component only -- "~" or
 * "~/rest") to $HOME/%USERPROFILE%, since stat() (and Python's os.path.isdir(), which the
 * original deploy.py also never expanded this for) does none of that on its own -- a plain text
 * field has no shell in front of it to do it automatically like a terminal would. */
static void normalize_manual_path(const char *in, char *out, size_t out_size) {
    while (*in == ' ' || *in == '\t') in++;
    size_t len = strlen(in);
    while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t')) len--;

    if (len >= 1 && in[0] == '~' && (len == 1 || in[1] == '/' || in[1] == '\\')) {
#if defined(_WIN32)
        const char *home = getenv("USERPROFILE");
#else
        const char *home = getenv("HOME");
#endif
        if (home && *home) {
            const char *rest = in + 1;
            size_t rest_len = len - 1;
            snprintf(out, out_size, "%s%.*s", home, (int)rest_len, rest);
            return;
        }
    }
    size_t n = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, in, n);
    out[n] = '\0';
}

static void rescan_sd(WizardState *w) {
    sd_candidates_free(&w->found_cards);
    sd_candidates_free(&w->all_mounts);
    sd_find_twilightmenu_cards(&w->found_cards);
    if (w->found_cards.count == 0) sd_list_mount_candidates(&w->all_mounts);
}

WizardState *wizard_create(void) {
    WizardState *w = (WizardState *)calloc(1, sizeof(WizardState));
    if (app_dir(w->app_dir, sizeof(w->app_dir)) != 0) {
        snprintf(w->app_dir, sizeof(w->app_dir), ".");
    }
    w->selected_theme = -1;
    w->step = STEP_SD_PICK;
    wizard_mutex_init(&w->scrape_mutex);
    rescan_sd(w);
    return w;
}

/* Joins the background scrape thread if one is still outstanding, cancelling it first so it
 * doesn't run forever. Safe to call whether or not a thread was ever started. */
static void join_scrape_thread_if_any(WizardState *w) {
    if (!w->scrape_thread_started) return;
    w->scrape_cancel = 1;
#if defined(_WIN32)
    WaitForSingleObject(w->scrape_thread, INFINITE);
    CloseHandle(w->scrape_thread);
#else
    pthread_join(w->scrape_thread, NULL);
#endif
    w->scrape_thread_started = 0;
}

void wizard_destroy(WizardState *w) {
    if (!w) return;
    join_scrape_thread_if_any(w);
    wizard_mutex_destroy(&w->scrape_mutex);
    scrape_preview_free(&w->scrape_preview);
    sd_candidates_free(&w->found_cards);
    sd_candidates_free(&w->all_mounts);
    theme_scan_free(&w->themes);
    free(w);
}

static void select_sd(WizardState *w, const char *path) {
    snprintf(w->chosen_sd, sizeof(w->chosen_sd), "%s", path);
    w->step = STEP_ALREADY_INSTALLED;
}

static void finish_and_go_done(WizardState *w) {
    sd_eject_hint(w->chosen_sd, w->eject_hint, sizeof(w->eject_hint));
    w->step = STEP_DONE;
}

/* --------------------------------------- do the install -------------------------------------
 * Mirrors deploy_srldr_and_themes() + install_default_theme() from deploy.py: create
 * _nds/TWiLightMenu, install build/dsimenu.srldr next to the app if present (backing up any
 * existing one), copy the chosen theme onto the SD (merge, never deletes), back up and update
 * settings.ini's [SRLOADER] THEME/DSI_THEME keys. */
/* `manual_srldr_path` (may be NULL/empty) is the "select a dsimenu.srldr manually" override from
 * the theme-pick screen -- if given, it's used INSTEAD of the auto-detected build/dsimenu.srldr
 * (a bad manual path is a hard error, logged and the whole srldr step skipped, rather than
 * silently falling back to auto-detect and installing a file the user didn't ask for); if blank,
 * behaves exactly like before this option existed. */
static void do_install(WizardState *w, const char *manual_srldr_path) {
    w->log_len = 0;
    w->log[0] = '\0';

    char dsimenu_dir[1200];
    path_join(dsimenu_dir, sizeof(dsimenu_dir), w->chosen_sd, DSIMENU_SUBPATH);
    fs_makedirs(dsimenu_dir);

    char srldr_src[1200];
    int have_srldr_src;
    if (manual_srldr_path && manual_srldr_path[0]) {
        normalize_manual_path(manual_srldr_path, srldr_src, sizeof(srldr_src));
        have_srldr_src = path_is_file(srldr_src);
        if (have_srldr_src) {
            log_line(w, "Using manually selected dsimenu.srldr: %s", srldr_src);
        } else {
            log_line(w, "ERROR: '%s' is not a dsimenu.srldr file I could find -- skipping this step.",
                      srldr_src);
        }
    } else {
        path_join(srldr_src, sizeof(srldr_src), w->app_dir, "build/dsimenu.srldr");
        have_srldr_src = path_is_file(srldr_src);
        if (!have_srldr_src) {
            log_line(w, "No dsimenu.srldr found in build/ -- skipping this step (fine if you only want the theme).");
        }
    }

    if (have_srldr_src) {
        char srldr_dst[1200];
        path_join(srldr_dst, sizeof(srldr_dst), w->chosen_sd, DSIMENU_SUBPATH "/dsimenu.srldr");
        if (path_is_file(srldr_dst)) {
            char bak[1210];
            snprintf(bak, sizeof(bak), "%s.bak", srldr_dst);
            copytree_copy_file(srldr_dst, bak);
            log_line(w, "Backed up previous dsimenu.srldr -> dsimenu.srldr.bak");
        }
        if (copytree_copy_file(srldr_src, srldr_dst) == 0) {
            log_line(w, "dsimenu.srldr installed.");
        } else {
            log_line(w, "ERROR copying dsimenu.srldr.");
        }
    }

    char assets_dir[1200];
    path_join(assets_dir, sizeof(assets_dir), w->chosen_sd, DSIMENU_ASSETS_SUBPATH);
    fs_makedirs(assets_dir);

    if (w->selected_theme >= 0 && (size_t)w->selected_theme < w->themes.count) {
        const BundledTheme *theme = &w->themes.items[w->selected_theme];

        char themes_dst[1200];
        path_join(themes_dst, sizeof(themes_dst), assets_dir, "themes");
        char theme_dst[1400];
        path_join(theme_dst, sizeof(theme_dst), themes_dst, theme->name);
        fs_makedirs(theme_dst);

        log_line(w, "Installing theme '%s'...", theme->name);
        if (copytree_merge(theme->path, theme_dst) == 0) {
            log_line(w, "Theme copied to %s", theme_dst);
        } else {
            log_line(w, "ERROR copying the theme.");
        }

        char settings_path[1300];
        path_join(settings_path, sizeof(settings_path), w->chosen_sd, SETTINGS_INI_SUBPATH);
        if (path_is_file(settings_path)) {
            char bak[1310];
            snprintf(bak, sizeof(bak), "%s.bak", settings_path);
            copytree_copy_file(settings_path, bak);
            log_line(w, "Backed up previous settings.ini -> settings.ini.bak");
        }

        IniLines lines;
        ini_read(settings_path, &lines);
        IniKV kv[2] = {{"THEME", "0"}, {"DSI_THEME", theme->name}};
        ini_set_keys(&lines, "SRLOADER", kv, 2);
        if (ini_write(settings_path, &lines) == 0) {
            log_line(w, "settings.ini updated: [SRLOADER] THEME = 0, DSI_THEME = %s", theme->name);
        } else {
            log_line(w, "ERROR writing settings.ini.");
        }
        ini_free(&lines);
    } else {
        log_line(w, "No theme selected -- skipping theme install.");
    }

    w->step = STEP_SCRAPE_ASK;
}

/* ------------------------------------------ actions ------------------------------------------ */

void wizard_rescan_sd(WizardState *w) { rescan_sd(w); }

void wizard_select_sd(WizardState *w, const char *path) { select_sd(w, path); }

void wizard_pick_manual_sd(WizardState *w, const char *raw_path) {
    char resolved[1024];
    normalize_manual_path(raw_path, resolved, sizeof(resolved));

    /* A path typed as e.g. "/Documents/dscard" looks absolute but almost always isn't meant to
     * be rooted at the filesystem root -- it's what a lot of people type when they mean "my
     * Documents folder", i.e. $HOME/Documents/dscard. normalize_manual_path() above only expands
     * a literal leading "~" for this reason (same as deploy.py always did); if the literal path
     * doesn't exist, retry it once relative to $HOME before giving up. */
    if (resolved[0] == '/' && !path_is_dir(resolved)) {
#if defined(_WIN32)
        const char *home = getenv("USERPROFILE");
#else
        const char *home = getenv("HOME");
#endif
        if (home && *home) {
            char with_home[1024];
            snprintf(with_home, sizeof(with_home), "%s%s", home, resolved);
            if (path_is_dir(with_home)) snprintf(resolved, sizeof(resolved), "%s", with_home);
        }
    }

    if (resolved[0] == '\0') {
        snprintf(w->sd_pick_error, sizeof(w->sd_pick_error), "Enter a path first.");
    } else if (!path_is_dir(resolved)) {
        snprintf(w->sd_pick_error, sizeof(w->sd_pick_error),
                 "'%s' isn't a folder I could access.", resolved);
    } else {
        w->sd_pick_error[0] = '\0';
        select_sd(w, resolved);
    }
}

void wizard_confirm_already_installed(WizardState *w, int already_installed) {
    if (already_installed) {
        char assets_dir[1200];
        path_join(assets_dir, sizeof(assets_dir), w->chosen_sd, DSIMENU_ASSETS_SUBPATH);
        fs_makedirs(assets_dir); /* scrape-only, mirrors deploy.py's --scrape-only makedirs */
        w->step = STEP_SCRAPE_ASK;
    } else {
        theme_scan_free(&w->themes);
        theme_scan_bundled(w->app_dir, &w->themes);
        w->selected_theme = w->themes.count > 0 ? 0 : -1;
        w->step = STEP_THEME_PICK;
    }
}

void wizard_select_theme(WizardState *w, int index) {
    if (index >= 0 && (size_t)index < w->themes.count) w->selected_theme = index;
}

void wizard_go_back_to_already_installed(WizardState *w) { w->step = STEP_ALREADY_INSTALLED; }

void wizard_install(WizardState *w, const char *manual_srldr_path) { do_install(w, manual_srldr_path); }

void wizard_go_back_to_sd_pick(WizardState *w) { w->step = STEP_SD_PICK; }

/* --------------------------------- logo scraping (src/scrape.c) ------------------------------- */

static void scrape_progress_cb(size_t done, size_t total, const char *rom_name,
                                const char *detail, void *user_data) {
    WizardState *w = (WizardState *)user_data;
    wizard_mutex_lock(&w->scrape_mutex);
    w->scrape_done = done;
    w->scrape_total = total;
    snprintf(w->scrape_current_name, sizeof(w->scrape_current_name), "%s", rom_name ? rom_name : "");
    snprintf(w->scrape_current_detail, sizeof(w->scrape_current_detail), "%s", detail ? detail : "");
    wizard_mutex_unlock(&w->scrape_mutex);
}

/* SCRAPE_JOB_DOWNLOAD only: the current game's own logo download (see scrape_run()'s
 * on_byte_progress). Reset to (0, 0, 0) at the start of every game by scrape_run() itself, so
 * this doesn't need to guess when a new item started. */
static void scrape_byte_progress_cb(size_t bytes_done, size_t bytes_total, double speed_bps,
                                     void *user_data) {
    WizardState *w = (WizardState *)user_data;
    wizard_mutex_lock(&w->scrape_mutex);
    w->scrape_item_bytes_done = bytes_done;
    w->scrape_item_bytes_total = bytes_total;
    w->scrape_item_speed_bps = speed_bps;
    wizard_mutex_unlock(&w->scrape_mutex);
}

/* SCRAPE_JOB_LOGIN_CHECK: validates scrape_pending_user/pass against ScreenScraper's account API
 * on a background thread so the UI can show a loading screen instead of freezing for the network
 * round-trip. Leaves scrape_job_message empty on an accepted login. */
static void scrape_login_check_worker_run(WizardState *w) {
    char message[256];
    message[0] = '\0';

    ScreenScraperCreds creds;
    if (screenscraper_creds_from_env(&creds) == 0) {
        snprintf(creds.ssid, sizeof(creds.ssid), "%s", w->scrape_pending_user);
        snprintf(creds.sspassword, sizeof(creds.sspassword), "%s", w->scrape_pending_pass);
        screenscraper_validate_login(&creds, message, sizeof(message));
    }

    wizard_mutex_lock(&w->scrape_mutex);
    snprintf(w->scrape_job_message, sizeof(w->scrape_job_message), "%s", message);
    w->scrape_finished = 1;
    wizard_mutex_unlock(&w->scrape_mutex);
}

/* SCRAPE_JOB_SCAN: scans+hashes the SD on a background thread (can be slow on a card with
 * thousands of ROMs) so the UI can show real scan progress instead of freezing. */
static void scrape_scan_worker_run(WizardState *w) {
    char cache_dir[1200];
    path_join(cache_dir, sizeof(cache_dir), w->app_dir, "cache");
    char blocklist_path[1200];
    path_join(blocklist_path, sizeof(blocklist_path), w->app_dir, "data/system_blocklist.txt");

    scrape_preview_free(&w->scrape_preview);
    scrape_preview(w->chosen_sd, w->scrape_assets_dir, cache_dir, blocklist_path,
                   scrape_progress_cb, w, &w->scrape_cancel, &w->scrape_preview);

    wizard_mutex_lock(&w->scrape_mutex);
    w->scrape_finished = 1;
    wizard_mutex_unlock(&w->scrape_mutex);
}

/* SCRAPE_JOB_DOWNLOAD: the actual network scrape, same as before this job/step split existed. */
static void scrape_download_worker_run(WizardState *w) {
    ScrapeStats stats;
    char err[256];
    err[0] = '\0';
    scrape_run(&w->scrape_preview, w->scrape_assets_dir, w->scrape_force, scrape_progress_cb,
               scrape_byte_progress_cb, w, &w->scrape_cancel, &stats, err, sizeof(err));

    wizard_mutex_lock(&w->scrape_mutex);
    w->scrape_stats = stats;
    snprintf(w->scrape_run_error, sizeof(w->scrape_run_error), "%s", err);
    w->scrape_finished = 1;
    wizard_mutex_unlock(&w->scrape_mutex);
}

static void scrape_worker_dispatch(WizardState *w) {
    switch (w->scrape_job) {
        case SCRAPE_JOB_LOGIN_CHECK: scrape_login_check_worker_run(w); break;
        case SCRAPE_JOB_SCAN: scrape_scan_worker_run(w); break;
        case SCRAPE_JOB_DOWNLOAD: scrape_download_worker_run(w); break;
        case SCRAPE_JOB_NONE: break;
    }
}

#if defined(_WIN32)
static DWORD WINAPI scrape_thread_main(LPVOID arg) {
    scrape_worker_dispatch((WizardState *)arg);
    return 0;
}
#else
static void *scrape_thread_main(void *arg) {
    scrape_worker_dispatch((WizardState *)arg);
    return NULL;
}
#endif

/* Starts scrape_thread_main() for whichever job the caller already stored in w->scrape_job.
 * Caller is responsible for resetting that job's own input/output fields first. */
static int start_scrape_thread(WizardState *w) {
#if defined(_WIN32)
    w->scrape_thread = CreateThread(NULL, 0, scrape_thread_main, w, 0, NULL);
    w->scrape_thread_started = (w->scrape_thread != NULL);
#else
    w->scrape_thread_started = (pthread_create(&w->scrape_thread, NULL, scrape_thread_main, w) == 0);
#endif
    return w->scrape_thread_started;
}

void wizard_scrape_ask_yes(WizardState *w) {
    w->scrape_saved_user[0] = '\0';
    w->scrape_has_saved_password = 0;
    w->scrape_login_error[0] = '\0';
    screenscraper_login_cache_load(w->app_dir, w->scrape_saved_user, sizeof(w->scrape_saved_user),
                                    NULL, 0, &w->scrape_has_saved_password);
    w->step = STEP_SCRAPE_LOGIN;
}

void wizard_scrape_skip(WizardState *w) { finish_and_go_done(w); }

/* Kicks off the SD scan+hash as a background job and advances to scrape_scanning -- shared by
 * every path that leaves the scrape_login step (logged in, anonymous, or the login-check job
 * finishing with an accepted login; wizard_confirm_already_installed() reaching this before the
 * login screen existed at all is no longer reachable, but this is also reused by
 * wizard_scrape_login_continue()/_skip() and wizard_state_json()'s login-check completion). */
static void start_scrape_scan_job(WizardState *w) {
    if (w->scrape_thread_started) return; /* guard against a double click */
    path_join(w->scrape_assets_dir, sizeof(w->scrape_assets_dir), w->chosen_sd, DSIMENU_ASSETS_SUBPATH);

    w->scrape_job = SCRAPE_JOB_SCAN;
    w->scrape_cancel = 0;
    w->scrape_finished = 0;
    w->scrape_done = 0;
    w->scrape_total = 0;
    w->scrape_current_name[0] = '\0';
    w->scrape_current_detail[0] = '\0';
    start_scrape_thread(w);
    w->step = STEP_SCRAPE_SCANNING;
}

/* Commits an accepted (or unchecked, e.g. no devid configured) login: exports SS_USER/SS_PASS for
 * screenscraper_lookup_game() to pick up (see screenscraper_creds_from_env()), and updates the
 * on-disk cache if asked to remember it. */
static void apply_pending_login(WizardState *w) {
    setenv("SS_USER", w->scrape_pending_user, 1);
    setenv("SS_PASS", w->scrape_pending_pass, 1);
    if (w->scrape_pending_remember) {
        screenscraper_login_cache_save(w->app_dir, w->scrape_pending_user, w->scrape_pending_pass);
    }
    w->scrape_pending_pass[0] = '\0';
}

void wizard_scrape_login_continue(WizardState *w, const char *user, const char *pass, int remember) {
    if (!user || !user[0]) {
        wizard_scrape_login_skip(w);
        return;
    }

    char resolved_pass[128];
    resolved_pass[0] = '\0';
    if (pass && pass[0]) {
        snprintf(resolved_pass, sizeof(resolved_pass), "%s", pass);
    } else {
        /* Password field left blank -- reuse the cached one for this exact username, if any
         * (the standard "leave blank to keep it unchanged" convention). */
        char cached_user[128];
        int has_pass = 0;
        if (screenscraper_login_cache_load(w->app_dir, cached_user, sizeof(cached_user),
                                            resolved_pass, sizeof(resolved_pass), &has_pass) == 0 &&
            has_pass && strcmp(cached_user, user) == 0) {
            /* resolved_pass now holds the cached password. */
        } else {
            resolved_pass[0] = '\0';
        }
    }

    snprintf(w->scrape_pending_user, sizeof(w->scrape_pending_user), "%s", user);
    snprintf(w->scrape_pending_pass, sizeof(w->scrape_pending_pass), "%s", resolved_pass);
    w->scrape_pending_remember = remember;

    /* Only ScreenScraper itself can confirm a login is right, and that needs the app-level
     * devid/devpassword too (every request does, login check included) -- if those aren't
     * configured (e.g. a dev build with no INJECTOR_SS_DEVID set), there's no way to validate
     * anything, so skip straight to applying the credentials and scanning, same as before this
     * feedback existed. */
    ScreenScraperCreds creds;
    if (screenscraper_creds_from_env(&creds) != 0) {
        w->scrape_login_error[0] = '\0';
        apply_pending_login(w);
        start_scrape_scan_job(w);
        return;
    }

    if (w->scrape_thread_started) return; /* guard against a double click */
    w->scrape_job = SCRAPE_JOB_LOGIN_CHECK;
    w->scrape_cancel = 0;
    w->scrape_finished = 0;
    w->scrape_job_message[0] = '\0';
    start_scrape_thread(w);
    w->step = STEP_SCRAPE_LOGIN_CHECKING;
}

void wizard_scrape_login_skip(WizardState *w) {
    w->scrape_login_error[0] = '\0';
    unsetenv("SS_USER");
    unsetenv("SS_PASS");
    start_scrape_scan_job(w);
}

void wizard_scrape_login_forget(WizardState *w) {
    screenscraper_login_cache_clear(w->app_dir);
    w->scrape_saved_user[0] = '\0';
    w->scrape_has_saved_password = 0;
}

void wizard_scrape_start(WizardState *w, int force) {
    if (w->scrape_thread_started) return; /* guard against a double click */

    w->scrape_job = SCRAPE_JOB_DOWNLOAD;
    w->scrape_force = force;
    w->scrape_cancel = 0;
    w->scrape_finished = 0;
    w->scrape_done = 0;
    w->scrape_total = w->scrape_preview.count;
    w->scrape_current_name[0] = '\0';
    w->scrape_current_detail[0] = '\0';
    w->scrape_item_bytes_done = 0;
    w->scrape_item_bytes_total = 0;
    w->scrape_item_speed_bps = 0;
    memset(&w->scrape_stats, 0, sizeof(w->scrape_stats));
    w->scrape_run_error[0] = '\0';

    start_scrape_thread(w);
    w->step = STEP_SCRAPE_PROGRESS;
}

void wizard_scrape_cancel(WizardState *w) { w->scrape_cancel = 1; }

void wizard_scrape_done_ok(WizardState *w) { finish_and_go_done(w); }

void wizard_restart(WizardState *w) {
    join_scrape_thread_if_any(w);
    scrape_preview_free(&w->scrape_preview);

    w->step = STEP_SD_PICK;
    w->chosen_sd[0] = '\0';
    w->sd_pick_error[0] = '\0';
    w->selected_theme = -1;
    w->log_len = 0;
    w->log[0] = '\0';
    w->eject_hint[0] = '\0';
    w->scrape_saved_user[0] = '\0';
    w->scrape_has_saved_password = 0;
    w->scrape_login_error[0] = '\0';
    w->scrape_pending_user[0] = '\0';
    w->scrape_pending_pass[0] = '\0';
    w->scrape_pending_remember = 0;
    w->scrape_job = SCRAPE_JOB_NONE;
    w->scrape_job_message[0] = '\0';
    w->scrape_force = 0;
    rescan_sd(w);
}

/* ---------------------------------------- JSON state ------------------------------------------ */

static cJSON *paths_to_json(const SdCandidates *c) {
    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < c->count; i++) cJSON_AddItemToArray(arr, cJSON_CreateString(c->paths[i]));
    return arr;
}

char *wizard_state_json(WizardState *w) {
    /* The frontend polls getState() on an interval while a background job is running (any of the
     * three loading/progress steps below); this is the one natural place to notice the worker
     * thread finished and decide what happens next -- each job's own completion handling. */
    int scan_running = (w->scrape_job == SCRAPE_JOB_SCAN && w->scrape_thread_started);
    if (w->scrape_thread_started &&
        (w->step == STEP_SCRAPE_LOGIN_CHECKING || w->step == STEP_SCRAPE_SCANNING ||
         w->step == STEP_SCRAPE_PROGRESS)) {
        wizard_mutex_lock(&w->scrape_mutex);
        int finished = w->scrape_finished;
        wizard_mutex_unlock(&w->scrape_mutex);
        if (finished) {
            ScrapeJob job = w->scrape_job;
            join_scrape_thread_if_any(w);
            scan_running = 0;
            w->scrape_job = SCRAPE_JOB_NONE;
            switch (job) {
                case SCRAPE_JOB_LOGIN_CHECK:
                    snprintf(w->scrape_login_error, sizeof(w->scrape_login_error), "%s",
                             w->scrape_job_message);
                    if (w->scrape_login_error[0]) {
                        w->step = STEP_SCRAPE_LOGIN; /* show the error, let them retry */
                    } else {
                        apply_pending_login(w);
                        start_scrape_scan_job(w);
                        scan_running = (w->scrape_job == SCRAPE_JOB_SCAN && w->scrape_thread_started);
                    }
                    break;
                case SCRAPE_JOB_SCAN:
                    w->step = STEP_SCRAPE_PREVIEW;
                    break;
                case SCRAPE_JOB_DOWNLOAD:
                    w->step = STEP_SCRAPE_DONE;
                    sd_eject_hint(w->chosen_sd, w->eject_hint, sizeof(w->eject_hint));
                    break;
                case SCRAPE_JOB_NONE:
                    break;
            }
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "step", step_name(w->step));
    cJSON_AddStringToObject(root, "appDir", w->app_dir);
    cJSON_AddItemToObject(root, "foundCards", paths_to_json(&w->found_cards));
    cJSON_AddItemToObject(root, "allMounts", paths_to_json(&w->all_mounts));
    cJSON_AddStringToObject(root, "chosenSd", w->chosen_sd);
    cJSON_AddStringToObject(root, "sdPickError", w->sd_pick_error);

    cJSON *themes = cJSON_CreateArray();
    for (size_t i = 0; i < w->themes.count; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddNumberToObject(t, "index", (double)i);
        cJSON_AddStringToObject(t, "name", w->themes.items[i].name);
        cJSON_AddItemToArray(themes, t);
    }
    cJSON_AddItemToObject(root, "themes", themes);
    cJSON_AddNumberToObject(root, "selectedTheme", w->selected_theme);

    cJSON_AddStringToObject(root, "log", w->log);
    cJSON_AddStringToObject(root, "ejectHint", w->eject_hint);

    /* scrape_preview is being mutated on the background scan thread right now (realloc'd, counted
     * up) whenever scan_running is true -- reading any of it here before it's joined would race,
     * so it's skipped entirely until the scan finishes (the UI has nothing to show for it until
     * then anyway; scrape_scanning only renders scrape.progress*). */
    cJSON *scrape = cJSON_CreateObject();
    cJSON_AddNumberToObject(scrape, "totalRoms", (double)(scan_running ? 0 : w->scrape_preview.total_rom_count));
    cJSON_AddNumberToObject(scrape, "blockedCount", (double)(scan_running ? 0 : w->scrape_preview.blocked_count));
    cJSON_AddStringToObject(scrape, "savedUser", w->scrape_saved_user);
    cJSON_AddBoolToObject(scrape, "hasSavedPassword", w->scrape_has_saved_password);
    cJSON_AddStringToObject(scrape, "loginError", w->scrape_login_error);

    cJSON *games = cJSON_CreateArray();
    if (!scan_running) {
        for (size_t i = 0; i < w->scrape_preview.count; i++) {
            cJSON *g = cJSON_CreateObject();
            cJSON_AddStringToObject(g, "romName", w->scrape_preview.games[i].rom_name);
            cJSON_AddNumberToObject(g, "duplicateCount", (double)w->scrape_preview.games[i].duplicate_count);
            cJSON_AddBoolToObject(g, "hasLogoAlready", w->scrape_preview.games[i].has_logo_already);
            cJSON_AddItemToArray(games, g);
        }
    }
    cJSON_AddItemToObject(scrape, "games", games);

    wizard_mutex_lock(&w->scrape_mutex);
    cJSON_AddNumberToObject(scrape, "progressDone", (double)w->scrape_done);
    cJSON_AddNumberToObject(scrape, "progressTotal", (double)w->scrape_total);
    cJSON_AddStringToObject(scrape, "currentName", w->scrape_current_name);
    cJSON_AddStringToObject(scrape, "currentDetail", w->scrape_current_detail);
    cJSON_AddStringToObject(scrape, "runError", w->scrape_run_error);
    cJSON_AddNumberToObject(scrape, "itemBytesDone", (double)w->scrape_item_bytes_done);
    cJSON_AddNumberToObject(scrape, "itemBytesTotal", (double)w->scrape_item_bytes_total);
    cJSON_AddNumberToObject(scrape, "itemSpeedBps", w->scrape_item_speed_bps);
    wizard_mutex_unlock(&w->scrape_mutex);

    cJSON *stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "downloaded", (double)w->scrape_stats.downloaded);
    cJSON_AddNumberToObject(stats, "alreadyHadLogo", (double)w->scrape_stats.already_had_logo);
    cJSON_AddNumberToObject(stats, "notFound", (double)w->scrape_stats.not_found);
    cJSON_AddNumberToObject(stats, "noArt", (double)w->scrape_stats.no_art);
    cJSON_AddNumberToObject(stats, "failed", (double)w->scrape_stats.failed);
    cJSON_AddItemToObject(scrape, "stats", stats);

    cJSON_AddItemToObject(root, "scrape", scrape);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
