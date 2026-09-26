/* Round-trip test for src/copytree.c against merge_copy_tree()'s documented behavior:
 * recursive copy, skip .DS_Store/._*, preserve mtime, never delete pre-existing dst content. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <utime.h>

#include "copytree.h"

static int all_ok = 1;

static void check(const char *what, int cond) {
    printf("[%s] %s\n", cond ? "OK" : "FAIL", what);
    all_ok &= cond;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    fputs(content, f);
    fclose(f);
}

int main(void) {
    system("rm -rf /tmp/injector_test_copytree");
    system("mkdir -p /tmp/injector_test_copytree/src/sub");
    system("mkdir -p /tmp/injector_test_copytree/dst");

    write_file("/tmp/injector_test_copytree/src/top.txt", "top-level file");
    write_file("/tmp/injector_test_copytree/src/sub/nested.txt", "nested file");
    write_file("/tmp/injector_test_copytree/src/.DS_Store", "should be skipped");
    write_file("/tmp/injector_test_copytree/src/._AppleDouble", "should also be skipped");
    write_file("/tmp/injector_test_copytree/dst/preexisting.txt", "must survive the merge");

    /* Give the source file a distinctive, non-"just now" mtime so preservation is actually
     * verifiable (not just "happens to match because both timestamps are close to now"). */
    struct utimbuf old_time;
    old_time.actime = 1000000000; /* 2001-09-09, arbitrary but far from "now" */
    old_time.modtime = 1000000000;
    utime("/tmp/injector_test_copytree/src/top.txt", &old_time);

    int rc = copytree_merge("/tmp/injector_test_copytree/src", "/tmp/injector_test_copytree/dst");
    check("copytree_merge returns 0", rc == 0);

    check("top.txt copied", file_exists("/tmp/injector_test_copytree/dst/top.txt"));
    check("sub/nested.txt copied", file_exists("/tmp/injector_test_copytree/dst/sub/nested.txt"));
    check(".DS_Store skipped", !file_exists("/tmp/injector_test_copytree/dst/.DS_Store"));
    check("._AppleDouble skipped", !file_exists("/tmp/injector_test_copytree/dst/._AppleDouble"));
    check("pre-existing dst file survives the merge",
          file_exists("/tmp/injector_test_copytree/dst/preexisting.txt"));

    char *top_content = read_file("/tmp/injector_test_copytree/dst/top.txt");
    check("top.txt content matches", top_content && strcmp(top_content, "top-level file") == 0);
    free(top_content);

    char *nested_content = read_file("/tmp/injector_test_copytree/dst/sub/nested.txt");
    check("nested.txt content matches", nested_content && strcmp(nested_content, "nested file") == 0);
    free(nested_content);

    char *preexisting = read_file("/tmp/injector_test_copytree/dst/preexisting.txt");
    check("pre-existing content unchanged", preexisting && strcmp(preexisting, "must survive the merge") == 0);
    free(preexisting);

    struct stat st;
    stat("/tmp/injector_test_copytree/dst/top.txt", &st);
    check("mtime preserved on copy", st.st_mtime == 1000000000);

    /* Re-running (merge on top of itself) must not fail or delete anything. */
    rc = copytree_merge("/tmp/injector_test_copytree/src", "/tmp/injector_test_copytree/dst");
    check("re-running copytree_merge still returns 0", rc == 0);
    check("pre-existing dst file still survives a second merge",
          file_exists("/tmp/injector_test_copytree/dst/preexisting.txt"));

    printf(all_ok ? "\nALL TESTS PASSED\n" : "\nSOME TESTS FAILED\n");
    system("rm -rf /tmp/injector_test_copytree");
    return all_ok ? 0 : 1;
}
