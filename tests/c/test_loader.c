/*
 * DLL loading (src/host/tm_loader.c) and GetTempPathA (tm_kernel32.c),
 * through the registered shims as the game calls them. A synthetic
 * application directory is mapped as drive Q:, a user directory as D:;
 * no game files.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "tm_loader.h"
#include "tm_kernel32.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}

static void k_Export1(void) { RET(0x1111u, 0); }
static const win32hle_shim test_shims[] = { { "testhost.dll!Export1", k_Export1 }, { 0, 0 } };
static const tm_host_export test_exports[] = { { "Export1", "testhost.dll!Export1", NULL }, { 0, 0, 0 } };
static const tm_host_dll test_dll = { .name = "testhost.dll", .loadable = 1, .exports = test_exports };

static char *g_str;                          /* a guest string buffer */
static uint32_t gs(const char *s) { strcpy(g_str, s); return hle_va(g_str); }
static uint32_t call(const char *name, int n, const uint32_t *args) {
    return hle_call_guest(hle_resolve(name), n, args);
}
static uint32_t load(const char *name) { uint32_t a[1] = { gs(name) }; return call("tm:LoadLibraryA", 1, a); }
static uint32_t handle(const char *name) { uint32_t a[1] = { gs(name) }; return call("tm:GetModuleHandleA", 1, a); }
static uint32_t freelib(uint32_t h) { uint32_t a[1] = { h }; return call("tm:FreeLibrary", 1, a); }
static uint32_t proc(uint32_t h, const char *name) {
    static char *pn; if (!pn) pn = hle_alloc(64);
    uint32_t a[2] = { h, name ? (strcpy(pn, name), hle_va(pn)) : 7u };
    return call("tm:GetProcAddress", 2, a);
}

int main(void) {
    char app[] = "/tmp/tm-loader-app-XXXXXX", user[] = "/tmp/tm-loader-user-XXXXXX";
    if (!mkdtemp(app) || !mkdtemp(user)) return 1;
    char p[1200];
    snprintf(p, sizeof p, "%s/Plain.DLL", app);   FILE *f = fopen(p, "w"); if (f) fclose(f);
    hle_set_drive('q', app);
    hle_set_drive('d', user);
    recomp_host_init();
    win32hle_register(test_shims);
    win32hle_register(tm_loader_overrides);
    win32hle_register(tm_kernel32_overrides);
    tm_loader_add(&test_dll);
    tm_loader_set_app_dir("Q:\\Game.exe");
    mach_enter();
    g_str = hle_alloc(1024);
    char g[1024];

    /* where a name is answered from (search order) */
    check("bare host DLL, not loaded: system dir", tm_loader_locate("testhost.dll", g, sizeof g) == TM_LOAD_HOST_DLL);
    check("no extension -> .dll", tm_loader_locate("TESTHOST", g, sizeof g) == TM_LOAD_HOST_DLL);
    check("trailing dot = no extension, not a host DLL", tm_loader_locate("testhost.", g, sizeof g) == TM_LOAD_WIN32HLE);
    check("file in the application dir, any case", tm_loader_locate("plain.dll", g, sizeof g) == TM_LOAD_FILE &&
          !strcmp(g, "Q:\\plain.dll"));
    check("system dir path", tm_loader_locate("c:\\windows\\system32\\TestHost.dll", g, sizeof g) == TM_LOAD_HOST_DLL);
    check("other path: only there", tm_loader_locate("C:\\elsewhere\\testhost.dll", g, sizeof g) == TM_LOAD_WIN32HLE);
    check("win32hle system DLL", tm_loader_locate("kernel32.dll", g, sizeof g) == TM_LOAD_WIN32HLE);
    check("empty name", tm_loader_locate("", g, sizeof g) == TM_LOAD_NOT_FOUND);

    /* the application dir comes before the system dir */
    snprintf(p, sizeof p, "%s/testhost.dll", app); f = fopen(p, "w"); if (f) fclose(f);
    hle_path_changed();                          /* made behind win32hle's back: drop its kept listings */
    check("a copy in the application dir wins", tm_loader_locate("testhost.dll", g, sizeof g) == TM_LOAD_FILE);
    unlink(p);
    hle_path_changed();

    /* reference counts and unloading */
    check("not loaded: GetModuleHandleA NULL", handle("testhost.dll") == 0 && hle_get_last_error() == 126);
    uint32_t h = load("TestHost.DLL");
    check("LoadLibraryA -> handle", h == 0x7D000000u && tm_loader_refs("testhost.dll") == 1);
    check("GetModuleHandleA finds it, no reference", handle("testhost") == h && tm_loader_refs("testhost.dll") == 1);
    check("second load: same handle, refs 2", load("testhost.dll") == h && tm_loader_refs("testhost.dll") == 2);
    uint32_t e = proc(h, "Export1");
    check("GetProcAddress by name", e && e == hle_resolve("testhost.dll!Export1"));
    check("exported function runs", e && hle_call_guest(e, 0, NULL) == 0x1111u);
    hle_set_last_error(0);
    check("unknown export: NULL, ERROR_PROC_NOT_FOUND", proc(h, "Export2") == 0 && hle_get_last_error() == 127);
    check("export names are case-sensitive", proc(h, "export1") == 0);
    check("ordinal: NULL, ERROR_PROC_NOT_FOUND", proc(h, NULL) == 0 && hle_get_last_error() == 127);

    uint32_t a3[3] = { h, hle_va(g_str), 260 };
    uint32_t n = call("tm:GetModuleFileNameA", 3, a3);
    check("GetModuleFileNameA", n == strlen("C:\\WINDOWS\\SYSTEM32\\testhost.dll") && !strcmp(g_str, "C:\\WINDOWS\\SYSTEM32\\testhost.dll"));
    a3[2] = 10;
    hle_set_last_error(0);
    n = call("tm:GetModuleFileNameA", 3, a3);
    check("GetModuleFileNameA truncated (Vista+)", n == 10 && strlen(g_str) == 9 && hle_get_last_error() == 122);

    check("FreeLibrary: refs 1", freelib(h) == 1 && tm_loader_refs("testhost.dll") == 1 && handle("testhost.dll") == h);
    check("FreeLibrary: unloaded", freelib(h) == 1 && tm_loader_refs("testhost.dll") == 0);
    hle_set_last_error(0);
    check("after unload: GetModuleHandleA NULL", handle("testhost.dll") == 0 && hle_get_last_error() == 126);
    hle_set_last_error(0);
    check("after unload: GetProcAddress fails, ERROR_MOD_NOT_FOUND", proc(h, "Export1") == 0 && hle_get_last_error() == 126);
    hle_set_last_error(0);
    check("stale handle: FreeLibrary FALSE, ERROR_MOD_NOT_FOUND", freelib(h) == 0 && hle_get_last_error() == 126);
    check("reload after unload", load("testhost.dll") == h && tm_loader_refs("testhost.dll") == 1);

    /* names left to win32hle */
    check("win32hle system DLL still loads", load("kernel32.dll") != 0);
    hle_set_last_error(0);
    check("missing DLL: NULL, ERROR_MOD_NOT_FOUND", load("nosuch.dll") == 0 && hle_get_last_error() == 126);

    /* GetTempPathA */
    uint32_t t[2] = { 260, hle_va(g_str) };
    n = call("tm:GetTempPathA", 2, t);
    check("GetTempPathA", n == strlen("D:\\Local Settings\\Temp\\") && !strcmp(g_str, "D:\\Local Settings\\Temp\\"));
    struct stat st;
    snprintf(p, sizeof p, "%s/Local Settings/Temp", user);
    check("temp directory created on the user drive", stat(p, &st) == 0 && S_ISDIR(st.st_mode));
    strcpy(g_str, "untouched");
    t[0] = 5;
    n = call("tm:GetTempPathA", 2, t);
    check("GetTempPathA small buffer: size needed, buffer untouched", n == strlen("D:\\Local Settings\\Temp\\") + 1 &&
          !strcmp(g_str, "untouched"));
    t[0] = 0; t[1] = 0;
    check("GetTempPathA NULL buffer: size needed", call("tm:GetTempPathA", 2, t) == strlen("D:\\Local Settings\\Temp\\") + 1);

    mach_leave();
    snprintf(p, sizeof p, "rm -rf '%s' '%s'", app, user);
    if (system(p)) return 1;
    printf("loader: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
