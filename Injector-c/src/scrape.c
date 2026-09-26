#include "scrape.h"
#include "rom_scan.h"
#include "blocklist.h"
#include "screenscraper.h"
#include "manifest.h"
#include "copytree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <strings.h> /* strcasecmp */
#endif

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *base = path;
    if (slash && slash + 1 > base) base = slash + 1;
    if (bslash && bslash + 1 > base) base = bslash + 1;
    return base;
}

static int path_is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void path_join(char *out, size_t out_size, const char *base, const char *rel) {
    size_t blen = strlen(base);
    while (blen > 0 && (base[blen - 1] == '/' || base[blen - 1] == '\\')) blen--;
    snprintf(out, out_size, "%.*s/%s", (int)blen, base, rel);
}

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *text = (char *)malloc((size_t)sz + 1);
    size_t n = fread(text, 1, (size_t)sz, f);
    text[n] = '\0';
    fclose(f);
    return text;
}

static int write_whole_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

static void rate_limit_sleep(void) {
    /* ScreenScraper's own request-rate limit: >=1.0s between requests, same 1.2s margin
     * Skyscraper's own limitTimer uses. Only called after an actual network lookup. */
#if defined(_WIN32)
    Sleep(1200);
#else
    struct timespec ts = {1, 200000000L};
    nanosleep(&ts, NULL);
#endif
}

/* --------------------------------------- preview ----------------------------------------- */

static ScrapeGame *preview_find_or_add(ScrapePreview *out, const RomHash *hash) {
    for (size_t i = 0; i < out->count; i++) {
        if (strcmp(out->games[i].hash.sha1, hash->sha1) == 0) return &out->games[i];
    }
    if (out->count >= out->capacity) {
        size_t new_cap = out->capacity ? out->capacity * 2 : 16;
        out->games = (ScrapeGame *)realloc(out->games, new_cap * sizeof(ScrapeGame));
        out->capacity = new_cap;
    }
    ScrapeGame *g = &out->games[out->count++];
    memset(g, 0, sizeof(*g));
    g->hash = *hash;
    return g;
}

static int cmp_game_name(const void *a, const void *b) {
    const ScrapeGame *ga = (const ScrapeGame *)a;
    const ScrapeGame *gb = (const ScrapeGame *)b;
#if defined(_WIN32)
    return _stricmp(ga->rom_name, gb->rom_name);
#else
    return strcasecmp(ga->rom_name, gb->rom_name);
#endif
}

void scrape_preview(const char *sd_root, const char *assets_root, const char *cache_dir,
                     const char *blocklist_path, ScrapeProgressFn on_progress, void *user_data,
                     const volatile int *should_cancel, ScrapePreview *out) {
    memset(out, 0, sizeof(*out));

    RomList roms;
    memset(&roms, 0, sizeof(roms));
    rom_scan_find(sd_root, &roms);
    out->total_rom_count = roms.count;

    Blocklist block;
    blocklist_load(blocklist_path, &block);

    fs_makedirs(cache_dir);
    RomHashCache *cache = romhash_cache_load(cache_dir);
    int cache_dirty = 0;

    for (size_t i = 0; i < roms.count; i++) {
        if (should_cancel && *should_cancel) break;
        const char *path = roms.paths[i];
        if (blocklist_name_blocked(&block, path)) {
            out->blocked_count++;
            if (on_progress) on_progress(i + 1, roms.count, basename_of(path), NULL, user_data);
            continue;
        }
        RomHash hash;
        int was_cached = 0;
        if (romhash_file_cached(cache, path, &hash, &was_cached) != 0) {
            if (on_progress) on_progress(i + 1, roms.count, basename_of(path), NULL, user_data);
            continue; /* unreadable file -- skip silently, same as the Python original */
        }
        if (!was_cached) cache_dirty = 1;
        if (blocklist_has_sha1(&block, hash.sha1)) {
            out->blocked_count++;
            if (on_progress) on_progress(i + 1, roms.count, basename_of(path), NULL, user_data);
            continue;
        }

        ScrapeGame *g = preview_find_or_add(out, &hash);
        if (g->duplicate_count == 0) {
            snprintf(g->primary_path, sizeof(g->primary_path), "%s", path);
            snprintf(g->rom_name, sizeof(g->rom_name), "%s", basename_of(path));
        }
        g->duplicate_count++;
        if (on_progress) on_progress(i + 1, roms.count, basename_of(path), NULL, user_data);
    }

    if (cache_dirty) romhash_cache_save(cache, cache_dir);
    romhash_cache_free(cache);
    blocklist_free(&block);
    rom_scan_free(&roms);

    char assets_dir[1300];
    path_join(assets_dir, sizeof(assets_dir), assets_root, "assets");
    for (size_t i = 0; i < out->count; i++) {
        char game_dir[1350], logo_path[1400];
        path_join(game_dir, sizeof(game_dir), assets_dir, out->games[i].hash.sha1);
        path_join(logo_path, sizeof(logo_path), game_dir, "logo.png");
        out->games[i].has_logo_already = path_is_file(logo_path);
    }

    if (out->count > 1) qsort(out->games, out->count, sizeof(ScrapeGame), cmp_game_name);
}

void scrape_preview_free(ScrapePreview *p) {
    if (!p) return;
    free(p->games);
    memset(p, 0, sizeof(*p));
}

/* ----------------------------------------- run -------------------------------------------- */

int scrape_run(const ScrapePreview *preview, const char *assets_root, int force,
                ScrapeProgressFn on_progress, HttpProgressFn on_byte_progress, void *user_data,
                const volatile int *should_cancel, ScrapeStats *stats, char *err, size_t err_size) {
    memset(stats, 0, sizeof(*stats));
    if (err && err_size) err[0] = '\0';
    stats->total = preview->count;

    /* Return value ignored on purpose: even without INJECTOR_SS_DEVID/DEVPASSWORD configured, the
     * request still goes out over curl for every game -- ScreenScraper's own response (or the
     * network layer) explains the failure per game via lookup.message/stats->failed, instead of
     * this function refusing to try at all. */
    ScreenScraperCreds creds;
    screenscraper_creds_from_env(&creds);

    char assets_dir[1300];
    path_join(assets_dir, sizeof(assets_dir), assets_root, "assets");
    if (fs_makedirs(assets_dir) != 0) {
        if (err) snprintf(err, err_size, "Could not create %s", assets_dir);
        return -1;
    }

    char manifest_path[1300], index_path[1300];
    path_join(manifest_path, sizeof(manifest_path), assets_root, "manifest.yml");
    path_join(index_path, sizeof(index_path), assets_root, "assets_index.yml");

    /* Load whatever already exists so games untouched by this run round-trip instead of being
     * dropped from the rewritten files. */
    Manifest manifest;
    manifest_init(&manifest);
    char *manifest_text = read_whole_file(manifest_path);
    if (manifest_text) {
        manifest_load(manifest_text, &manifest);
        free(manifest_text);
    }

    AssetsIndex index;
    assets_index_init(&index);
    char *index_text = read_whole_file(index_path);
    if (index_text) {
        assets_index_load(index_text, &index);
        free(index_text);
    }

    for (size_t i = 0; i < preview->count; i++) {
        if (should_cancel && *should_cancel) break;
        const ScrapeGame *g = &preview->games[i];

        if (on_progress) on_progress(i, preview->count, g->rom_name, NULL, user_data);
        if (on_byte_progress) on_byte_progress(0, 0, 0, user_data);

        char game_dir[1350], logo_path[1400];
        path_join(game_dir, sizeof(game_dir), assets_dir, g->hash.sha1);
        path_join(logo_path, sizeof(logo_path), game_dir, "logo.png");

        char rel_logo[128];
        rel_logo[0] = '\0';

        if (path_is_file(logo_path) && !force) {
            stats->already_had_logo++;
            snprintf(rel_logo, sizeof(rel_logo), "assets/%s/logo.png", g->hash.sha1);
            if (on_progress) on_progress(i + 1, preview->count, g->rom_name, "already had a logo", user_data);
        } else {
            ScreenScraperLookup lookup;
            screenscraper_lookup_game(&creds, &g->hash, g->hash.size, g->rom_name, &lookup);

            if (lookup.status == SCREENSCRAPER_ERROR) {
                stats->failed++;
                if (on_progress) on_progress(i + 1, preview->count, g->rom_name, lookup.message, user_data);
            } else if (lookup.status == SCREENSCRAPER_NOT_FOUND) {
                stats->not_found++;
                if (on_progress) on_progress(i + 1, preview->count, g->rom_name, "game not found", user_data);
            } else if (lookup.wheel_url[0] == '\0') {
                stats->no_art++;
                if (on_progress) on_progress(i + 1, preview->count, g->rom_name, "no logo available", user_data);
            } else {
                unsigned char *data = NULL;
                size_t size = 0;
                char dl_err[256];
                if (screenscraper_download_logo(lookup.wheel_url, on_byte_progress, user_data,
                                                 &data, &size, dl_err, sizeof(dl_err)) == 0) {
                    fs_makedirs(game_dir);
                    if (write_whole_file(logo_path, data, size) == 0) {
                        snprintf(rel_logo, sizeof(rel_logo), "assets/%s/logo.png", g->hash.sha1);
                        stats->downloaded++;
                        if (on_progress) on_progress(i + 1, preview->count, g->rom_name, "downloaded", user_data);
                    } else {
                        stats->failed++;
                        if (on_progress) on_progress(i + 1, preview->count, g->rom_name, "failed to write logo.png", user_data);
                    }
                    free(data);
                } else {
                    stats->failed++;
                    if (on_progress) on_progress(i + 1, preview->count, g->rom_name, dl_err, user_data);
                }
            }
            rate_limit_sleep();
        }

        ManifestGame *mg = NULL;
        for (size_t m = 0; m < manifest.game_count; m++) {
            if (strcmp(manifest.games[m].game_id, g->hash.sha1) == 0) {
                mg = &manifest.games[m];
                break;
            }
        }
        if (!mg) mg = manifest_add_game(&manifest);

        free(mg->game_id);
        mg->game_id = strdup(g->hash.sha1);
        mg->identity.size = g->hash.size;
        memcpy(mg->identity.sha1, g->hash.sha1, sizeof(mg->identity.sha1));
        memcpy(mg->identity.md5, g->hash.md5, sizeof(mg->identity.md5));
        memcpy(mg->identity.crc32, g->hash.crc32, sizeof(mg->identity.crc32));
        free(mg->rom_name);
        mg->rom_name = strdup(g->rom_name);
        free(mg->logo);
        mg->logo = rel_logo[0] ? strdup(rel_logo) : NULL;

        assets_index_set(&index, g->rom_name, g->hash.sha1);
    }

    char *out_manifest = manifest_dump(&manifest);
    char *out_index = assets_index_dump(&index);
    write_whole_file(manifest_path, out_manifest, strlen(out_manifest));
    write_whole_file(index_path, out_index, strlen(out_index));
    free(out_manifest);
    free(out_index);

    manifest_free(&manifest);
    assets_index_free(&index);
    return 0;
}
