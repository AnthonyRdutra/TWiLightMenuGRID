/* The install wizard: replicates deploy.py's interactive step sequence (SD pick -> already
 * installed? -> theme install -> scrape logos? -> done) as a small state machine, using the
 * Phase 1/2 core modules (sd/ini/copytree/scrape) for the actual disk I/O and network work.
 *
 * This module has no GUI framework dependency at all -- it exposes actions (button clicks) as
 * plain functions and the current screen as a JSON string. The actual rendering lives in
 * assets/ui/ (HTML/CSS/JS) driven from src/ui/bridge.c over a webview. */
#ifndef INJECTOR_UI_WIZARD_H
#define INJECTOR_UI_WIZARD_H

typedef struct WizardState WizardState;

WizardState *wizard_create(void);
void wizard_destroy(WizardState *w);

/* Returns a malloc'd JSON string describing everything the frontend needs to render the current
 * screen (step, SD candidates, themes, scrape preview/progress, log, errors, ...). Caller must
 * free() it. While the current step is one of the three background-job loading/progress screens
 * ("scrape_login_checking", "scrape_scanning", "scrape_progress"), this ALSO checks whether that
 * job's thread has finished and, if so, joins it and decides what happens next (retry the login
 * form, move on to the next job, or land on "scrape_done") -- the frontend polls getState() on an
 * interval while on any of those screens, so this is the one place that naturally happens without
 * a separate "poll" action the frontend would otherwise have to remember to call. */
char *wizard_state_json(WizardState *w);

/* ---------------------------------------- actions --------------------------------------------
 * Each mirrors a button/field a wizard screen has; the frontend calls one of these (via
 * bridge.c) in response to a click, then re-reads wizard_state_json() to re-render. */

void wizard_rescan_sd(WizardState *w);

/* Selects an SD path already offered in found_cards/all_mounts and advances to the
 * "already installed?" step. */
void wizard_select_sd(WizardState *w, const char *path);

/* Validates+normalizes a manually-typed path ("~" expansion, existence check, and a fallback
 * retry relative to $HOME for paths that look absolute but aren't) and, on success, behaves like
 * wizard_select_sd(); on failure sets sd_pick_error instead of advancing. */
void wizard_pick_manual_sd(WizardState *w, const char *raw_path);

/* "Is TWiLightMenuGRID already installed on this card?" -- true skips straight to the scrape-ask
 * step (mirrors deploy.py's --scrape-only), false scans bundled themes and picks the
 * theme-pick step. */
void wizard_confirm_already_installed(WizardState *w, int already_installed);

void wizard_select_theme(WizardState *w, int index);

/* "< Back" on the theme-pick step -- back to "already installed?", not all the way to SD
 * pick, since the chosen SD is still valid. */
void wizard_go_back_to_already_installed(WizardState *w);

/* Performs the install (mirrors deploy_srldr_and_themes()+install_default_theme() from
 * deploy.py) and advances to the scrape-ask step. `manual_srldr_path` (may be NULL/empty) lets
 * the theme-pick screen's "select a dsimenu.srldr manually" field override the auto-detected
 * "<app_dir>/build/dsimenu.srldr" -- "~" is expanded the same way a manual SD path is (see
 * wizard_pick_manual_sd()); a manual path that doesn't resolve to a real file is a hard error
 * (logged, srldr step skipped) rather than silently falling back to auto-detect. */
void wizard_install(WizardState *w, const char *manual_srldr_path);

void wizard_go_back_to_sd_pick(WizardState *w);

/* --------------------------------- logo scraping (src/scrape.c) ------------------------------- */

/* "Fetch game logos now?" -- yes always goes straight to the scrape-login step so the user can
 * optionally sign in with their OWN ScreenScraper account (ssid/sspassword) for a higher request
 * quota before the scan/scrape actually runs. Nothing here locally requires the app-level
 * INJECTOR_SS_DEVID/INJECTOR_SS_DEVPASSWORD credentials either -- scrape_run() (scrape.c) sends
 * the real request over curl regardless of whether they're set, and ScreenScraper's own response
 * explains a missing/wrong devid per game (stats->failed) instead of anything here refusing to
 * try. No does the same as wizard_scrape_skip(). */
void wizard_scrape_ask_yes(WizardState *w);
void wizard_scrape_skip(WizardState *w);

/* -------------------------- ScreenScraper end-user login (scrape-login step) ------------------- *
 * Optional: ScreenScraper works anonymously too, just with a lower request quota. Credentials are
 * cached at "<app_dir>/.ss_credentials.json" (see screenscraper.c) so this only needs to be typed
 * once. Both actions below finish entering the scrape flow the same way wizard_scrape_ask_yes()
 * used to on its own: scan+hash the SD (no network yet, on a background thread so the UI can show
 * "scrape_scanning" progress instead of freezing) and advance to scrape-preview. */

/* `user`/`pass` come from the login form; if `pass` is empty and a password was already cached
 * for this exact `user`, the cached one is reused (so leaving the password field blank keeps
 * whatever was saved, standard "unchanged" convention). The pair is validated against
 * ScreenScraper's own account API on a background thread (advancing to "scrape_login_checking" in
 * the meantime so the UI doesn't freeze for the network round-trip; skipped entirely -- straight
 * to applying the credentials and scanning -- if INJECTOR_SS_DEVID/DEVPASSWORD aren't set, see
 * wizard_scrape_ask_yes()'s comment); on rejection wizard_state_json() lands back on
 * STEP_SCRAPE_LOGIN with the reason in the JSON state's scrape.loginError instead of proceeding.
 * If `remember`, the resolved user/password are written to the cache once accepted; otherwise any
 * existing cache entry is left as-is. */
void wizard_scrape_login_continue(WizardState *w, const char *user, const char *pass, int remember);

/* "Continue without login" -- proceeds anonymously for this run without touching the cache. */
void wizard_scrape_login_skip(WizardState *w);

/* "Forget saved credentials" -- deletes the cache file; stays on the scrape-login step. */
void wizard_scrape_login_forget(WizardState *w);

/* Starts the actual scrape (network calls) on a background thread and advances to the
 * scrape-progress step; wizard_state_json() transitions to scrape-done once it finishes. `force`
 * re-fetches every game's logo, even ones that already have one (the "repor todos os logos"
 * option on the scrape-preview step) -- passed straight through to scrape_run()'s own `force`. */
void wizard_scrape_start(WizardState *w, int force);

/* Requests cancellation of the running scrape (polled between games, so it stops at the next
 * safe point rather than immediately). */
void wizard_scrape_cancel(WizardState *w);

/* "OK" on the scrape-done step. */
void wizard_scrape_done_ok(WizardState *w);

/* "Start over" on the done screen. */
void wizard_restart(WizardState *w);

#endif
