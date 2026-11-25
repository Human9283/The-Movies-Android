/*
 * GetDiskFreeSpaceExW (src/host/tm_kernel32.c), called
 * through its registered shim as the game calls it: stdcall, 4 arguments,
 * guest pointers. A synthetic directory is mapped as drive Q:; no game files.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "win32hle.h"
#include "tm_kernel32.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}

static uint16_t *g_wpath;              /* guest buffer for the path argument */
static unsigned char *g_out;           /* 3 x 8 bytes of output, with guard bytes */

/* UTF-8 -> UTF-16 (BMP only, enough for these tests) into g_wpath. */
static uint32_t wpath(const char *s) {
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        uint32_t c;
        if (*p < 0x80) c = *p++;
        else if ((*p & 0xE0) == 0xC0) c = (uint32_t)(*p & 0x1F) << 6 | (p[1] & 0x3F), p += 2;
        else c = (uint32_t)(*p & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F), p += 3;
        g_wpath[k++] = (uint16_t)c;
    }
    g_wpath[k] = 0;
    return hle_va(g_wpath);
}

#define OUT(i) (g_out + 1 + 9 * (i))   /* outputs at offsets 1, 10, 19: unaligned, guards between */

static uint32_t call(uint32_t path, int a, int t, int f) {
    memset(g_out, 0xA5, 32);
    uint32_t args[4] = { path, a ? hle_va(OUT(0)) : 0, t ? hle_va(OUT(1)) : 0, f ? hle_va(OUT(2)) : 0 };
    return hle_call_guest(hle_resolve("GetDiskFreeSpaceExW"), 4, args);
}
static uint64_t out(int i) { uint64_t v; memcpy(&v, OUT(i), 8); return v; }
static int untouched(int i) { for (int k = 0; k < 8; k++) if (OUT(i)[k] != 0xA5) return 0; return 1; }
static int guards(void) { return g_out[0] == 0xA5 && g_out[9] == 0xA5 && g_out[18] == 0xA5 && g_out[27] == 0xA5; }

int main(void) {
    char tmp[] = "/tmp/tm-disk-free-w-XXXXXX";
    if (!mkdtemp(tmp)) return 1;
    char p[1200];
    snprintf(p, sizeof p, "%s/sub", tmp);                         mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/\xD0\x9C\xD0\xBE\xD0\xB8 \xD0\xB4\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82\xD1\x8B", tmp);
    mkdir(p, 0755);                                              /* "Мои документы" */
    snprintf(p, sizeof p, "%s/file.txt", tmp);
    FILE *fp = fopen(p, "w"); if (fp) fputs("x", fp), fclose(fp);

    hle_set_drive('q', tmp);
    win32hle_register(tm_kernel32_shims);
    mach_enter();
    g_wpath = hle_alloc(2 * 1024);
    g_out = hle_alloc(32);
    if (!g_wpath || !g_out) return 1;

    struct statvfs fs;
    if (statvfs(tmp, &fs)) return 1;
    uint64_t unit = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
    uint64_t want_avail = unit * fs.f_bavail, want_total = unit * fs.f_blocks, want_free = unit * fs.f_bfree;

    /* valid paths: root, subdirectory with and without a trailing separator, non-ASCII */
    const char *valid[] = { "Q:\\", "q:\\sub", "Q:\\SUB\\", "Q:/sub/",
                            "Q:\\\xD0\x9C\xD0\xBE\xD0\xB8 \xD0\xB4\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82\xD1\x8B" };
    for (size_t i = 0; i < sizeof valid / sizeof *valid; i++) {
        hle_set_last_error(42);
        uint32_t r = call(wpath(valid[i]), 1, 1, 1);
        char l[96]; snprintf(l, sizeof l, "valid path #%zu", i);
        check(l, r != 0);
        /* the volume can change between statvfs calls by a few blocks; totals cannot */
        check("total bytes, full 64 bits", out(1) == want_total);
        check("available <= free <= total", out(0) <= out(2) && out(2) <= out(1));
        check("free close to statvfs", out(2) + (1u << 26) >= want_free && out(2) <= want_free + (1u << 26));
        check("available close to statvfs", out(0) + (1u << 26) >= want_avail && out(0) <= want_avail + (1u << 26));
        check("last error untouched on success", hle_get_last_error() == 42);
        check("guard bytes", guards());
    }

    /* partial outputs: each one alone, and none */
    check("only available", call(wpath("Q:\\"), 1, 0, 0) && out(0) && untouched(1) && untouched(2) && guards());
    check("only total", call(wpath("Q:\\"), 0, 1, 0) && untouched(0) && out(1) == want_total && untouched(2));
    check("only total free", call(wpath("Q:\\"), 0, 0, 1) && untouched(0) && untouched(1) && out(2) <= want_total);
    check("no outputs", call(wpath("Q:\\"), 0, 0, 0) != 0 && untouched(0) && untouched(1) && untouched(2));

    /* NULL path: the root of the current drive */
    char old[1024];
    if (!getcwd(old, sizeof old)) return 1;
    snprintf(p, sizeof p, "%s/sub", tmp);
    if (chdir(p)) return 1;
    check("NULL path, cwd on Q:", call(0, 1, 1, 1) != 0 && out(1) == want_total);
    if (chdir(old)) return 1;

    /* failures: nothing written, GetLastError set */
    struct { const char *path; uint32_t err; } bad[] = {
        { "Q:\\missing", 2 },            /* last component missing: ERROR_FILE_NOT_FOUND */
        { "Q:\\missing\\", 2 },
        { "Q:\\missing\\deeper", 3 },    /* a directory above it missing: ERROR_PATH_NOT_FOUND */
        { "Q:\\file.txt", 267 },         /* a file: ERROR_DIRECTORY */
        { "R:\\", 3 },                   /* a drive that is not mapped */
        { "", 3 },                       /* empty string */
        { "\\\\server\\share\\", 50 },   /* UNC: not supported here */
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        hle_set_last_error(0);
        uint32_t r = call(wpath(bad[i].path), 1, 1, 1);
        char l[96]; snprintf(l, sizeof l, "failure \"%s\": returns 0, error %u (got %u)", bad[i].path, bad[i].err, hle_get_last_error());
        check(l, r == 0 && hle_get_last_error() == bad[i].err);
        check("failure writes no output", untouched(0) && untouched(1) && untouched(2));
    }

    mach_leave();
    hle_set_drive('q', NULL);
    snprintf(p, sizeof p, "rm -rf '%s'", tmp);
    if (system(p)) return 1;
    printf("GetDiskFreeSpaceExW: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
