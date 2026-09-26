/* Tests for the network-free half of src/screenscraper.c: screenscraper_parse_response()
 * against canned jeuInfos.php-shaped response bodies (both JSON and the plain-text error
 * bodies ScreenScraper sometimes sends instead). No network calls happen in this file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screenscraper.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

int main(void) {
    ScreenScraperLookup out;

    /* Region/type priority: wheel-hd@wor beats wheel@us and wheel@jp. */
    screenscraper_parse_response(
        "{\"header\":{\"success\":\"true\"},\"response\":{\"jeu\":{\"medias\":["
        "{\"type\":\"wheel\",\"region\":\"us\",\"url\":\"https://x/wheel-us.png\"},"
        "{\"type\":\"wheel-hd\",\"region\":\"wor\",\"url\":\"https://x/wheelhd-wor.png\"},"
        "{\"type\":\"wheel\",\"region\":\"jp\",\"url\":\"https://x/wheel-jp.png\"}"
        "]}}}",
        &out);
    check("valid response -> OK", out.status == SCREENSCRAPER_OK);
    check("picks wheel-hd/wor over lower-priority entries",
          strcmp(out.wheel_url, "https://x/wheelhd-wor.png") == 0);

    /* No priority-region match -- falls back to "any region" for the preferred type. */
    screenscraper_parse_response(
        "{\"header\":{\"success\":\"true\"},\"response\":{\"jeu\":{\"medias\":["
        "{\"type\":\"screenmarquee\",\"region\":\"wor\",\"url\":\"https://x/marquee.png\"},"
        "{\"type\":\"wheel\",\"region\":\"br\",\"url\":\"https://x/wheel-br.png\"}"
        "]}}}",
        &out);
    check("fallback: any-region match for the right type", out.status == SCREENSCRAPER_OK);
    check("fallback picks the wheel entry, ignoring the marquee",
          strcmp(out.wheel_url, "https://x/wheel-br.png") == 0);

    /* Game found, but no wheel/wheel-hd media at all -- OK with an empty url, not an error. */
    screenscraper_parse_response(
        "{\"header\":{\"success\":\"true\"},\"response\":{\"jeu\":{\"medias\":["
        "{\"type\":\"screenmarquee\",\"region\":\"wor\",\"url\":\"https://x/marquee.png\"}"
        "]}}}",
        &out);
    check("no wheel art -> still OK", out.status == SCREENSCRAPER_OK);
    check("no wheel art -> empty url", out.wheel_url[0] == '\0');

    /* jeu missing entirely -- game not in ScreenScraper's database. */
    screenscraper_parse_response("{\"header\":{\"success\":\"true\"},\"response\":{}}", &out);
    check("missing jeu -> NOT_FOUND", out.status == SCREENSCRAPER_NOT_FOUND);

    /* header.success == "false" -- surfaces header.error as the message. */
    screenscraper_parse_response(
        "{\"header\":{\"success\":\"false\",\"error\":\"Erro de autenticacao\"}}", &out);
    check("success:false -> ERROR", out.status == SCREENSCRAPER_ERROR);
    check("success:false -> message is header.error", strcmp(out.message, "Erro de autenticacao") == 0);

    /* Plain-text error bodies ScreenScraper sends instead of JSON for these conditions. */
    screenscraper_parse_response("Erreur : Jeu non trouv\xc3\xa9", &out);
    check("'non trouvee' text -> NOT_FOUND", out.status == SCREENSCRAPER_NOT_FOUND);

    screenscraper_parse_response("Votre quota de scrape est de 20000 requetes.", &out);
    check("quota text -> ERROR", out.status == SCREENSCRAPER_ERROR);

    screenscraper_parse_response("L'API totalement ferm\xc3\xa9""e pour maintenance", &out);
    check("'API totalement fermee' text -> ERROR", out.status == SCREENSCRAPER_ERROR);

    /* Malformed / empty input -- never crashes, always reports ERROR. */
    screenscraper_parse_response("{this is not json", &out);
    check("malformed JSON -> ERROR", out.status == SCREENSCRAPER_ERROR);

    screenscraper_parse_response("", &out);
    check("empty response -> ERROR", out.status == SCREENSCRAPER_ERROR);

    /* --- login validation parsing (screenscraper_parse_login_response) -- no network involved */
    char msg[256];

    screenscraper_parse_login_response(
        "{\"header\":{\"success\":\"true\"},\"response\":{\"ssuser\":{\"id\":\"42\"}}}",
        msg, sizeof(msg));
    check("accepted login -> empty message", msg[0] == '\0');

    screenscraper_parse_login_response(
        "{\"header\":{\"success\":\"false\",\"error\":\"Login ou mot de passe incorrect\"}}",
        msg, sizeof(msg));
    check("rejected login -> message set", msg[0] != '\0');
    check("rejected login -> message is header.error",
          strcmp(msg, "Login ou mot de passe incorrect") == 0);

    screenscraper_parse_login_response(
        "{\"header\":{\"success\":\"true\"},\"response\":{}}", msg, sizeof(msg));
    check("success but no ssuser -> message set (invalid login)", msg[0] != '\0');

    screenscraper_parse_login_response("Votre quota de scrape est de 20000 requetes.", msg,
                                        sizeof(msg));
    check("quota text -> message set", msg[0] != '\0');

    screenscraper_parse_login_response("{this is not json", msg, sizeof(msg));
    check("malformed JSON -> message set", msg[0] != '\0');

    screenscraper_parse_login_response("", msg, sizeof(msg));
    check("empty response -> message set", msg[0] != '\0');

    /* --- end-user login cache (screenscraper_login_cache_*) -- no network involved --------- */
    system("rm -rf /tmp/injector_test_login_cache");
    system("mkdir -p /tmp/injector_test_login_cache");
    const char *app_dir = "/tmp/injector_test_login_cache";

    char user[128], pass[128];
    int has_pass = 0;
    int rc = screenscraper_login_cache_load(app_dir, user, sizeof(user), pass, sizeof(pass), &has_pass);
    check("no cache file yet -> load fails", rc != 0);

    check("save succeeds", screenscraper_login_cache_save(app_dir, "playerOne", "hunter2") == 0);

    user[0] = pass[0] = '\0';
    has_pass = 0;
    rc = screenscraper_login_cache_load(app_dir, user, sizeof(user), pass, sizeof(pass), &has_pass);
    check("load after save succeeds", rc == 0);
    check("loaded username matches", strcmp(user, "playerOne") == 0);
    check("loaded password matches", has_pass && strcmp(pass, "hunter2") == 0);

    /* Caller asking only for the username (pass/has_pass NULL) must not crash and still work. */
    user[0] = '\0';
    rc = screenscraper_login_cache_load(app_dir, user, sizeof(user), NULL, 0, NULL);
    check("username-only load still works", rc == 0 && strcmp(user, "playerOne") == 0);

    screenscraper_login_cache_clear(app_dir);
    rc = screenscraper_login_cache_load(app_dir, user, sizeof(user), pass, sizeof(pass), &has_pass);
    check("load after clear fails", rc != 0);

    system("rm -rf /tmp/injector_test_login_cache");

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    return all_ok ? 0 : 1;
}
