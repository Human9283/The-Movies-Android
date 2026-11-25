/*
 * The guest's Windows directory (src/host/tm_sysroot.c and the hle_path
 * mount patch): path translation both ways, separation of the game, system
 * and user directories, writes, GetWindowsDirectoryA/GetSystemDirectoryA,
 * and the DLL search order through the system directory. Synthetic
 * directories only.
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

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}
static char G[PATH_MAX], S[PATH_MAX], U[PATH_MAX];
static void touch(const char *dir, const char *rel) {
    char p[PATH_MAX * 2]; snprintf(p, sizeof p, "%s/%s", dir, rel);
    FILE *f = fopen(p, "w"); if (f) fclose(f);
    hle_path_changed();
}
static int host_is(const char *guest, const char *dir, const char *rel) {
    char h[1024], want[PATH_MAX * 2];
    hle_host_path(guest, h, sizeof h);
    snprintf(want, sizeof want, "%s%s%s", dir, *rel ? "/" : "", rel);
    if (strcmp(h, want)) fprintf(stderr, "  %s -> %s (want %s)\n", guest, h, want);
    return !strcmp(h, want);
}
static int guest_is(const char *dir, const char *rel, const char *want) {
    char h[PATH_MAX * 2], g[1024];
    snprintf(h, sizeof h, "%s%s%s", dir, *rel ? "/" : "", rel);
    hle_guest_path(h, g, sizeof g);
    if (strcmp(g, want)) fprintf(stderr, "  %s -> %s (want %s)\n", h, g, want);
    return !strcmp(g, want);
}
static int count_entries(const char *dir) {
    char cmd[PATH_MAX + 32]; snprintf(cmd, sizeof cmd, "ls -A '%s' | wc -l", dir);
    FILE *p = popen(cmd, "r"); int n = -1; if (p) { if (fscanf(p, "%d", &n) != 1) n = -1; pclose(p); }
    return n;
}

int main(void) {
    char base[] = "/tmp/tm-sysroot-XXXXXX";
    if (!mkdtemp(base)) return 1;
    snprintf(G, sizeof G, "%s/game", base); snprintf(S, sizeof S, "%s/system", base); snprintf(U, sizeof U, "%s/user", base);
    mkdir(G, 0755); mkdir(U, 0755);
    touch(G, "Movies.exe");
    char p[PATH_MAX * 2];
    snprintf(p, sizeof p, "%s/Data", G); mkdir(p, 0755);

    /* separation */
    char why[4096], inner[PATH_MAX * 2];
    snprintf(inner, sizeof inner, "%s/sys", G); mkdir(inner, 0755);
    check("system inside the game directory: refused", !tm_sysroot_check_apart(G, inner, U, why, sizeof why));
    rmdir(inner);
    snprintf(inner, sizeof inner, "%s/sys", U); mkdir(inner, 0755);
    check("system inside the user directory: refused", !tm_sysroot_check_apart(G, inner, U, why, sizeof why));
    rmdir(inner);

    recomp_host_init();
    hle_set_drive('c', G);
    hle_set_drive('d', U);
    win32hle_register(tm_sysroot_overrides);
    win32hle_register(tm_loader_overrides);
    check("tm_sysroot_init", tm_sysroot_init(G, S, U));
    snprintf(p, sizeof p, "%s/SYSTEM32", S);
    struct stat st;
    check("SYSTEM32 created, empty: no DLL files put there", stat(p, &st) == 0 && S_ISDIR(st.st_mode) && count_entries(p) == 0);
    check("apart once created", tm_sysroot_check_apart(G, S, U, why, sizeof why));
    check("game = user: refused", !tm_sysroot_check_apart(G, S, G, why, sizeof why));

    /* guest -> host */
    check("C:\\WINDOWS -> system", host_is("C:\\WINDOWS", S, ""));
    check("C:\\WINDOWS\\SYSTEM32\\x.dll -> system/SYSTEM32", host_is("C:\\WINDOWS\\SYSTEM32\\x.dll", S, "SYSTEM32/x.dll"));
    check("case and / : c:/windows/system32", host_is("c:/windows/system32", S, "SYSTEM32"));
    check("C:\\Windows\\System32\\ trailing separator", host_is("C:\\Windows\\System32\\", S, "SYSTEM32"));
    check("C:\\WINDOWSX is not the mount", host_is("C:\\WINDOWSX\\a.txt", G, "WINDOWSX/a.txt"));
    check("C:\\Movies.exe -> game", host_is("C:\\Movies.exe", G, "Movies.exe"));
    check("C:\\data (any case) -> game", host_is("C:\\data", G, "Data"));
    check("D:\\x -> user", host_is("D:\\x", U, "x"));

    /* host -> guest */
    check("system/SYSTEM32/f -> C:\\WINDOWS\\SYSTEM32\\f", (touch(S, "SYSTEM32/f.txt"), guest_is(S, "SYSTEM32/f.txt", "C:\\WINDOWS\\SYSTEM32\\f.txt")));
    check("system -> C:\\WINDOWS", guest_is(S, "", "C:\\WINDOWS"));
    check("game file -> C:\\Movies.exe", guest_is(G, "Movies.exe", "C:\\Movies.exe"));
    check("game root -> C:\\", guest_is(G, "", "C:\\"));
    check("user -> D:", guest_is(U, "", "D:\\"));
    snprintf(p, sizeof p, "%s/SYSTEM32/f.txt", S); unlink(p); hle_path_changed();

    /* writes under C:\WINDOWS land in the system directory, never in the game */
    mach_enter();
    char *g = hle_alloc(512);
    strcpy(g, "C:\\WINDOWS\\TEMP");
    uint32_t a2[2] = { hle_va(g), 0 };
    check("CreateDirectoryA(C:\\WINDOWS\\TEMP)", hle_call_guest(hle_resolve("CreateDirectoryA"), 2, a2) == 1);
    snprintf(p, sizeof p, "%s/TEMP", S);
    check("  made in the system directory", stat(p, &st) == 0 && S_ISDIR(st.st_mode));
    snprintf(p, sizeof p, "%s/WINDOWS", G);
    check("  nothing in the game directory", stat(p, &st) != 0);

    /* GetWindowsDirectoryA / GetSystemDirectoryA */
    uint32_t a[2] = { hle_va(g), 260 };
    check("GetWindowsDirectoryA", hle_call_guest(hle_resolve("tm:GetWindowsDirectoryA"), 2, a) == 10 && !strcmp(g, "C:\\WINDOWS"));
    check("GetSystemDirectoryA", hle_call_guest(hle_resolve("tm:GetSystemDirectoryA"), 2, a) == 19 && !strcmp(g, "C:\\WINDOWS\\SYSTEM32"));
    strcpy(g, "untouched"); a[1] = 19;
    check("GetSystemDirectoryA small buffer: size needed, untouched",
          hle_call_guest(hle_resolve("tm:GetSystemDirectoryA"), 2, a) == 20 && !strcmp(g, "untouched"));
    a[0] = 0; a[1] = 0;
    check("GetWindowsDirectoryA NULL: size needed", hle_call_guest(hle_resolve("tm:GetWindowsDirectoryA"), 2, a) == 11);

    /* FindFirstFileA in SYSTEM32: only what is really there */
    strcpy(g, "C:\\WINDOWS\\SYSTEM32\\wmvcore.dll");
    char *fd = hle_alloc(512);
    uint32_t f2[2] = { hle_va(g), hle_va(fd) };
    check("FindFirstFileA(SYSTEM32\\wmvcore.dll): not found (nothing faked)",
          hle_call_guest(hle_resolve("FindFirstFileA"), 2, f2) == 0xFFFFFFFFu);

    /* DLL search order through the system directory */
    tm_loader_set_app_dir("C:\\Movies.exe");
    char gp[1024];
    touch(S, "SYSTEM32/sysonly.dll");
    check("a file in SYSTEM32 is found at step 2", tm_loader_locate("SysOnly", gp, sizeof gp) == TM_LOAD_FILE &&
          !strcmp(gp, "C:\\WINDOWS\\SYSTEM32\\sysonly.dll"));
    touch(G, "sysonly.dll");
    check("the application directory comes first", tm_loader_locate("sysonly.dll", gp, sizeof gp) == TM_LOAD_FILE &&
          !strcmp(gp, "C:\\sysonly.dll"));
    check("not found anywhere: left to win32hle", tm_loader_locate("nosuch.dll", gp, sizeof gp) == TM_LOAD_WIN32HLE);
    mach_leave();

    snprintf(p, sizeof p, "rm -rf '%s'", base);
    if (system(p)) return 1;
    printf("sysroot: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
