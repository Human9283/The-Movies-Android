/*
 * Boundary and failure paths of the virtual file system (src/host/tm_vfs.c)
 * and the host-DLL table (tm_loader.c): table capacity, allocation
 * failures, handle exhaustion, the life of a search handle, the errors of
 * GetFileAttributesA, FindFirstFile's DOS wildcard rules and mount
 * boundaries. Synthetic directories only.
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "tm_loader.h"
#include "tm_sysroot.h"
#include "tm_vfs.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}
static char G[PATH_MAX], S[PATH_MAX], U[PATH_MAX], B[PATH_MAX];
static void touch(const char *dir, const char *rel) {
    char p[PATH_MAX * 2]; snprintf(p, sizeof p, "%s/%s", dir, rel);
    FILE *f = fopen(p, "w"); if (f) { fputs("x", f); fclose(f); }
    hle_path_changed();
}
static void mkd(const char *dir, const char *rel) {
    char p[PATH_MAX * 2]; snprintf(p, sizeof p, "%s/%s", dir, rel); mkdir(p, 0755); hle_path_changed();
}

static char *g_str, *g_fd;
static uint32_t gs(const char *s) { strcpy(g_str, s); return hle_va(g_str); }
static uint32_t call(const char *name, int n, const uint32_t *args) { return hle_call_guest(hle_resolve(name), n, args); }
static uint32_t ffirst(const char *pat) { uint32_t a[2] = { gs(pat), hle_va(g_fd) }; return call("tm:FindFirstFileA", 2, a); }
static uint32_t fnext(uint32_t h) { uint32_t a[2] = { h, hle_va(g_fd) }; return call("tm:FindNextFileA", 2, a); }
static uint32_t fclose_(uint32_t h) { uint32_t a[1] = { h }; return call("tm:FindClose", 1, a); }
static int count(const char *pat) {
    uint32_t h = ffirst(pat);
    if (h == 0xFFFFFFFFu) return -1;
    int n = 1;
    while (fnext(h)) n++;
    fclose_(h);
    return n;
}
static uint32_t attrs(const char *p, uint32_t *err) {
    uint32_t a[1] = { gs(p) };
    hle_set_last_error(0);
    uint32_t r = call("tm:GetFileAttributesA", 1, a);
    *err = hle_get_last_error();
    return r;
}
static int attr_is(const char *p, uint32_t want, uint32_t want_err) {
    uint32_t err, a = attrs(p, &err);
    int ok = a == want && (want != 0xFFFFFFFFu || err == want_err);
    if (!ok) fprintf(stderr, "  GetFileAttributesA(%s) -> 0x%X err %u (want 0x%X err %u)\n", p, a, err, want, want_err);
    return ok;
}
static int host_is(const char *guest, const char *dir, const char *rel) {
    char h[1024], want[PATH_MAX * 2];
    hle_host_path(guest, h, sizeof h);
    snprintf(want, sizeof want, "%s%s%s", dir, *rel ? "/" : "", rel);
    if (strcmp(h, want)) fprintf(stderr, "  %s -> %s (want %s)\n", guest, h, want);
    return !strcmp(h, want);
}

/* host DLLs in C:\V, to fill the table */
static char g_names[TM_LOADER_MAX_DLLS + 1][16];
static tm_host_dll g_dlls[TM_LOADER_MAX_DLLS + 1];
static const tm_host_export g_none[] = { { 0, 0, 0 } };

int main(void) {
    char base[] = "/tmp/tm-vfs-audit-XXXXXX";
    if (!mkdtemp(base)) return 1;
    snprintf(B, sizeof B, "%s", base);
    snprintf(G, sizeof G, "%s/game", base); snprintf(S, sizeof S, "%s/system", base); snprintf(U, sizeof U, "%s/user", base);
    mkdir(G, 0755); mkdir(U, 0755);
    touch(G, "Movies.exe"); mkd(G, "Data"); touch(G, "Data/a.ini"); mkd(G, "V"); mkd(G, "WINDOWS_OLD");
    touch(G, "WINDOWS_OLD/x.txt");
    recomp_host_init();
    hle_set_drive('c', G);
    hle_set_drive('d', U);
    win32hle_register(tm_vfs_overrides);
    win32hle_register(tm_loader_overrides);
    if (chdir(G)) return 1;
    check("tm_sysroot_init", tm_sysroot_init(G, S, U));
    mach_enter();
    g_str = hle_alloc(1024);
    g_fd = hle_alloc(320);
    char p[PATH_MAX * 2], why[1024];

    /* ---- capacity: mounts (8, C:\WINDOWS is the first) ---- */
    int mounted = 1;
    for (int i = 1; i <= 8; i++) {
        char g[64], h[PATH_MAX * 2];
        snprintf(g, sizeof g, "C:\\V\\M%d", i);
        snprintf(h, sizeof h, "%s/m%d", base, i); mkdir(h, 0755);
        int ok = tm_vfs_mount(g, h, why, sizeof why);
        if (i <= 6) check("mount below capacity", ok);
        else if (i == 7) check("mount at exact capacity (8th)", ok);
        else check("mount beyond capacity (9th): refused", !ok && strstr(why, "too many"));
        mounted += ok;
    }
    check("8 mounts in all", mounted == 8);
    char longleaf[300] = "C:\\V\\";
    memset(longleaf + 5, 'L', 70); longleaf[75] = 0;
    check("a mount whose last component is too long: refused", !tm_vfs_mount(longleaf, B, why, sizeof why));
    check("a mount of a drive root: refused", !tm_vfs_mount("C:\\", B, why, sizeof why));
    check("a mount with no host directory: refused", !tm_vfs_mount("C:\\Z", NULL, why, sizeof why));

    /* ---- capacity: host DLLs (TM_LOADER_MAX_DLLS) ---- */
    int added = 0;
    for (int i = 0; i <= TM_LOADER_MAX_DLLS; i++) {
        snprintf(g_names[i], sizeof g_names[i], "v%02d.dll", i);
        g_dlls[i] = (tm_host_dll){ .name = g_names[i], .dir = "C:\\V", .visible = 1, .loadable = 1, .exports = g_none };
        int ok = tm_loader_add(&g_dlls[i]);
        if (i < TM_LOADER_MAX_DLLS - 1) check("host DLL below capacity", ok);
        else if (i == TM_LOADER_MAX_DLLS - 1) check("host DLL at exact capacity", ok);
        else check("host DLL beyond capacity: refused", !ok);
        added += ok;
    }
    check("exactly TM_LOADER_MAX_DLLS registered", added == TM_LOADER_MAX_DLLS && tm_loader_count() == TM_LOADER_MAX_DLLS);
    check("a host DLL registered twice: refused", !tm_loader_add(&g_dlls[0]));
    /* C:\V holds 7 mounts and 16 host DLLs: 23 virtual entries, more than the
     * 16 an earlier version kept (it dropped the rest without a word) */
    check("C:\\V lists every virtual entry (. .. + 7 mounts + 16 DLLs)", count("C:\\V\\*") == 2 + 7 + TM_LOADER_MAX_DLLS);
    check("the last DLL registered is listed", attr_is("C:\\V\\v15.dll", 0x20, 0));
    check("the 17th, refused, is not", attr_is("C:\\V\\v16.dll", 0xFFFFFFFFu, 2));
    check("the last mount is listed and is a directory", attr_is("C:\\V\\M7", 0x10, 0));

    /* ---- allocation failures in FindFirstFileA ---- */
    tm_vfs_test_fail_alloc(0);
    check("the search state cannot be allocated: ERROR_NOT_ENOUGH_MEMORY",
          ffirst("C:\\Data\\*") == 0xFFFFFFFFu && hle_get_last_error() == 8);
    tm_vfs_test_fail_alloc(1);
    check("an entry cannot be added (state half made): ERROR_NOT_ENOUGH_MEMORY",
          ffirst("C:\\Data\\*") == 0xFFFFFFFFu && hle_get_last_error() == 8);
    tm_vfs_test_fail_alloc(1);
    check("the virtual entries cannot be copied: ERROR_NOT_ENOUGH_MEMORY",
          ffirst("C:\\V\\*") == 0xFFFFFFFFu && hle_get_last_error() == 8);
    tm_vfs_test_fail_alloc(-1);
    check("after the failures, a search works", count("C:\\Data\\*") == 3);

    /* ---- handle exhaustion ---- */
    static uint32_t held[4096];
    int nheld = 0;
    uint32_t h;
    while ((h = hle_handle_alloc(HLE_H_OTHER, (void *)1)) != 0) held[nheld++] = h;
    check("handle table full: ERROR_TOO_MANY_OPEN_FILES",
          ffirst("C:\\Data\\*") == 0xFFFFFFFFu && hle_get_last_error() == 4);
    hle_handle_close(held[--nheld]);
    h = ffirst("C:\\Data\\*");
    check("one handle free: the search gets it", h != 0xFFFFFFFFu);
    check("  and it closes", fclose_(h) == 1);
    while (nheld) hle_handle_close(held[--nheld]);

    /* ---- the life of a search handle ---- */
    h = ffirst("C:\\Data\\*");                    /* . .. a.ini */
    check("FindFirstFileA: the first entry is .", h != 0xFFFFFFFFu && !strcmp(g_fd + 44, "."));
    check("FindNextFileA: ..", fnext(h) && !strcmp(g_fd + 44, ".."));
    check("FindNextFileA: a.ini", fnext(h) && !strcmp(g_fd + 44, "a.ini"));
    hle_set_last_error(0);
    check("after the last: FALSE, ERROR_NO_MORE_FILES", !fnext(h) && hle_get_last_error() == 18);
    hle_set_last_error(0);
    check("again: still ERROR_NO_MORE_FILES (the handle stays valid)", !fnext(h) && hle_get_last_error() == 18);
    uint32_t nd[2] = { h, 0 };
    check("FindNextFileA with no data buffer: ERROR_INVALID_PARAMETER",
          !call("tm:FindNextFileA", 2, nd) && hle_get_last_error() == 87);
    check("FindClose: TRUE", fclose_(h) == 1);
    hle_set_last_error(0);
    check("FindNextFileA after FindClose: ERROR_INVALID_HANDLE", !fnext(h) && hle_get_last_error() == 6);
    hle_set_last_error(0);
    check("FindClose again: FALSE, ERROR_INVALID_HANDLE", fclose_(h) == 0 && hle_get_last_error() == 6);
    hle_set_last_error(0);
    check("FindClose of a value that was never a handle: ERROR_INVALID_HANDLE", fclose_(0x12345u) == 0 && hle_get_last_error() == 6);
    uint32_t cf[7] = { gs("C:\\Data\\a.ini"), 0x80000000u, 1, 0, 3, 0x80, 0 };
    uint32_t file = call("CreateFileA", 7, cf);
    hle_set_last_error(0);
    check("FindClose of a file handle: FALSE, ERROR_INVALID_HANDLE", file != 0xFFFFFFFFu && fclose_(file) == 0 &&
          hle_get_last_error() == 6);
    uint32_t ch[1] = { file };
    check("  and the file handle is still open", call("CloseHandle", 1, ch) == 1);
    h = ffirst("C:\\Data\\a.ini");
    check("no wildcard, one match: found", h != 0xFFFFFFFFu && !strcmp(g_fd + 44, "a.ini"));
    hle_set_last_error(0);
    check("  then ERROR_NO_MORE_FILES", !fnext(h) && hle_get_last_error() == 18);
    fclose_(h);
    memset(g_fd, 0x55, 320);
    check("no match: ERROR_FILE_NOT_FOUND", ffirst("C:\\Data\\*.zzz") == 0xFFFFFFFFu && hle_get_last_error() == 2);
    check("a trailing separator: ERROR_FILE_NOT_FOUND", ffirst("C:\\Data\\") == 0xFFFFFFFFu && hle_get_last_error() == 2);
    check("an empty pattern: ERROR_FILE_NOT_FOUND", ffirst("") == 0xFFFFFFFFu && hle_get_last_error() == 2);
    uint32_t np[2] = { 0, hle_va(g_fd) };
    check("a NULL pattern: ERROR_INVALID_PARAMETER", call("tm:FindFirstFileA", 2, np) == 0xFFFFFFFFu && hle_get_last_error() == 87);
    char lng[400] = "C:\\Data\\";
    memset(lng + 8, 'a', 300); lng[308] = 0;
    check("a pattern of MAX_PATH or more: ERROR_FILENAME_EXCED_RANGE", ffirst(lng) == 0xFFFFFFFFu && hle_get_last_error() == 206);
    check("a mount root searched: C:\\WINDOWS\\*", count("C:\\WINDOWS\\*") == 3);
    check("the root of C: searched by its mount's name", count("C:\\WIN*") == 2);   /* WINDOWS, WINDOWS_OLD */

    /* ---- GetFileAttributesA ---- */
    check("a file", attr_is("C:\\Movies.exe", 0x20, 0));
    check("a directory", attr_is("C:\\Data", 0x10, 0));
    check("a directory with a trailing separator", attr_is("C:\\Data\\", 0x10, 0));
    check("the root", attr_is("C:\\", 0x10, 0));
    check("a missing file in a directory: ERROR_FILE_NOT_FOUND", attr_is("C:\\Data\\nope.ini", 0xFFFFFFFFu, 2));
    check("a missing directory: ERROR_FILE_NOT_FOUND (its parent exists)", attr_is("C:\\NoDir", 0xFFFFFFFFu, 2));
    check("a name under a missing directory: ERROR_PATH_NOT_FOUND", attr_is("C:\\NoDir\\x.ini", 0xFFFFFFFFu, 3));
    check("a drive with no mapping: ERROR_PATH_NOT_FOUND", attr_is("Q:\\x.ini", 0xFFFFFFFFu, 3));
    check("an empty name: ERROR_PATH_NOT_FOUND", attr_is("", 0xFFFFFFFFu, 3));
    check("MAX_PATH or longer: ERROR_FILENAME_EXCED_RANGE", attr_is(lng, 0xFFFFFFFFu, 206));
    check("the mount root", attr_is("C:\\WINDOWS", 0x10, 0));
    check("a host DLL entry", attr_is("c:/windows/system32/V00.DLL", 0xFFFFFFFFu, 2));   /* v00 is in C:\V, not SYSTEM32 */
    check("a host DLL entry in its own directory", attr_is("C:\\v\\V00.DLL", 0x20, 0));
    check("a name below a host DLL entry: ERROR_PATH_NOT_FOUND", attr_is("C:\\V\\v00.dll\\x", 0xFFFFFFFFu, 3));
    uint32_t err;
    uint32_t a1[1] = { 0 };
    hle_set_last_error(0);
    check("NULL: ERROR_PATH_NOT_FOUND", call("tm:GetFileAttributesA", 1, a1) == 0xFFFFFFFFu && hle_get_last_error() == 3);
    (void)err;

    /* ---- wildcards (FindFirstFile's DOS rules) ---- */
    struct { const char *mask, *name; int want; } w[] = {
        { "*", "a", 1 }, { "*", "a.b.c", 1 }, { "*.*", "noext", 1 }, { "*.*", "a.b", 1 },
        { "*.", "noext", 1 }, { "*.", "a.txt", 0 }, { "*.", "a.b.c", 0 },
        { "?", "a", 1 }, { "?", "ab", 0 },
        { "a??", "a", 1 }, { "a??", "ab", 1 }, { "a??", "abc", 1 }, { "a??", "abcd", 0 },
        { "a?.txt", "a.txt", 1 }, { "a?.txt", "ab.txt", 1 }, { "a?.txt", "abc.txt", 0 },
        { "*.txt", "a.txt", 1 }, { "*.txt", "a.txt.bak", 0 }, { "*.TXT", "b.txt", 1 },
        { "*.c", "a.b.c", 1 }, { "a.*", "a", 1 }, { "a.*", "a.b.c", 1 }, { "a.*", "ab", 0 },
        { "*.t?t", "a.txt", 1 }, { "*.t?t", "a.tt", 0 }, { "*.??", "a.b", 1 }, { "*.??", "a", 1 },
        { "*a*", "bab", 1 }, { "*a*", "bbb", 0 }, { "a*b*c", "aXbYc", 1 }, { "a*b*c", "aXcYb", 0 },
        { "*.", ".", 1 }, { "*", "..", 1 }, { "a*", ".", 0 },
        { "readme", "README", 1 }, { "readme", "readme.txt", 0 },
    };
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) {
        int got = tm_vfs_wild(w[i].mask, w[i].name);
        char l[128]; snprintf(l, sizeof l, "wildcard %s ~ \"%s\" = %d", w[i].mask, w[i].name, w[i].want);
        if (got != w[i].want) fprintf(stderr, "  %s, got %d\n", l, got);
        check(l, got == w[i].want);
    }
    char fx[64];
    check("mask rewrite: *. -> DOS_STAR", tm_vfs_fixup_mask("*.", fx, sizeof fx) && !strcmp(fx, "\1"));
    check("mask rewrite: *.* -> * DOS_DOT *", tm_vfs_fixup_mask("*.*", fx, sizeof fx) && !strcmp(fx, "*\3*"));
    check("mask rewrite: a? -> a DOS_QM", tm_vfs_fixup_mask("a?", fx, sizeof fx) && !strcmp(fx, "a\2"));
    check("mask rewrite: too long for the buffer: 0", !tm_vfs_fixup_mask("abcdef", fx, 4));

    /* ---- mount boundaries ---- */
    check("exact mount root", host_is("C:\\WINDOWS", S, ""));
    check("first component inside", host_is("C:\\WINDOWS\\SYSTEM32", S, "SYSTEM32"));
    check("a sibling with the mount as a prefix is not in it", host_is("C:\\WINDOWS_OLD\\x.txt", G, "WINDOWS_OLD/x.txt"));
    check("  and is listed from the game directory", attr_is("C:\\WINDOWS_OLD\\x.txt", 0x20, 0));
    check("repeated separators", host_is("C:\\\\WINDOWS\\\\\\SYSTEM32", S, "SYSTEM32"));
    check("mixed separators", host_is("C:/WINDOWS\\SYSTEM32/", S, "SYSTEM32"));
    check("drive letter case", host_is("c:\\WINDOWS", S, ""));
    check(". normalises to the mount root", host_is("C:\\WINDOWS\\.", S, ""));
    check("SYSTEM32\\.. normalises to the mount root", host_is("C:\\WINDOWS\\SYSTEM32\\..", S, ""));
    check(".. above the mount leaves it", host_is("C:\\WINDOWS\\..", G, ""));
    check("relative name into the mount", host_is("windows\\system32", S, "SYSTEM32"));
    check("C:name into the mount", host_is("C:windows", S, ""));
    check("\\name into the mount", host_is("\\WINDOWS", S, ""));
    check("the root of C: is not the mount", host_is("C:\\", G, ""));
    check("a nested mount: refused", !tm_vfs_mount("C:\\WINDOWS\\X", B, why, sizeof why));
    check("a mount around another: refused", !tm_vfs_mount("C:\\V", B, why, sizeof why));
    mach_leave();

    snprintf(p, sizeof p, "rm -rf '%s'", base);
    if (system(p)) return 1;
    printf("vfs audit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
