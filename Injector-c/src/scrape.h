/* Orchestrates a logo-only scrape pass: scan the SD for .nds ROMs -> hash (cached) -> filter the
 * blocklist -> group duplicates by sha1 -> (preview), then look each unique game up on
 * ScreenScraper and bind the logo by hash into assets/<sha1>/logo.png + manifest.yml +
 * assets_index.yml (run). Mirrors assetbind/scan_and_bind.py's logo-only path (LOGO_ONLY=1), with
 * one simplification: assets_index.yml only indexes each game's PRIMARY filename, not every
 * duplicate/renamed copy's name (the Python original indexes all of them) -- a known, documented
 * gap rather than an oversight; revisit if renamed-duplicate ROMs turn out to need name-fallback
 * matching at runtime. */
#ifndef INJECTOR_SCRAPE_H
#define INJECTOR_SCRAPE_H

#include <stddef.h>
#include "romhash.h"
#include "http.h"

typedef struct {
    RomHash hash;
    char primary_path[1200]; /* one representative file for this sha1 group */
    char rom_name[512];      /* basename(primary_path) */
    size_t duplicate_count;  /* files sharing this sha1 (including the primary) */
    int has_logo_already;    /* true if assets/<sha1>/logo.png already exists */
} ScrapeGame;

typedef struct {
    ScrapeGame *games;
    size_t count;
    size_t capacity;
    size_t blocked_count;    /* ROMs excluded by the blocklist (system/homebrew apps) */
    size_t total_rom_count;  /* .nds files found before blocklist filtering/deduping */
} ScrapePreview;

/* Called once per game, right before starting its lookup (done == index, detail == NULL) and
 * once right after it's resolved (done == index + 1, detail explains the outcome). `total` is
 * constant across a run. scrape_preview() below uses the same type for its own per-file scan
 * progress, one call per file (done == index + 1, detail always NULL). */
typedef void (*ScrapeProgressFn)(size_t done, size_t total, const char *rom_name,
                                  const char *detail, void *user_data);

/* Scans `sd_root` (recursively) for .nds ROMs, hashes them (cache under `cache_dir`), filters
 * `blocklist_path`, and groups duplicates by sha1 -- no network calls, but still slow enough on a
 * large card (thousands of files to hash) that a caller running this on a background thread
 * should report `on_progress`/`user_data` through to a loading screen. `assets_root` is checked
 * for pre-existing assets/<sha1>/logo.png files (normally the SD's DSIMENU_ASSETS_SUBPATH). Polls
 * `*should_cancel` (if non-NULL) between files. Zero-initialize `out` before calling; release
 * with scrape_preview_free(). */
void scrape_preview(const char *sd_root, const char *assets_root, const char *cache_dir,
                     const char *blocklist_path, ScrapeProgressFn on_progress, void *user_data,
                     const volatile int *should_cancel, ScrapePreview *out);

void scrape_preview_free(ScrapePreview *p);

typedef struct {
    size_t total;
    size_t downloaded;       /* logo fetched and written this run */
    size_t already_had_logo; /* skipped -- already had a logo and force wasn't set */
    size_t not_found;        /* game isn't in ScreenScraper's database */
    size_t no_art;           /* game found, but no wheel/logo art exists for it */
    size_t failed;           /* network/auth/quota/write error */
} ScrapeStats;

/* Runs the scrape for a previously computed `preview`. For each game missing a logo (or every
 * game, if `force`), calls ScreenScraper, writes `<assets_root>/assets/<sha1>/logo.png`, and
 * updates `<assets_root>/manifest.yml` + `<assets_root>/assets_index.yml` (existing files are
 * loaded first so untouched games round-trip instead of being dropped). Sleeps ~1.2s between
 * actual network lookups (ScreenScraper's own rate limit -- see screenscraper.c). Polls
 * `*should_cancel` (if non-NULL) between games so a background thread can be stopped early.
 * `on_byte_progress` (may be NULL) reports the CURRENT game's own logo download in flight -- see
 * HttpProgressFn in http.h; reset to (0, 0, 0) right as each new game starts (metadata lookup,
 * before the image download itself even begins) so a caller's per-item UI doesn't show the
 * previous game's stale numbers while the next one's lookup is in flight. Both callbacks share
 * the same `user_data`. Returns 0 if it ran (even with per-game failures -- see *stats for
 * those), -1 if a required directory couldn't be created (err set either way). */
int scrape_run(const ScrapePreview *preview, const char *assets_root, int force,
                ScrapeProgressFn on_progress, HttpProgressFn on_byte_progress, void *user_data,
                const volatile int *should_cancel, ScrapeStats *stats, char *err, size_t err_size);

#endif
