/*
 * The virtual file system (src/host/tm_vfs.c) and host DLLs (tm_loader.c,
 * tm_wmvcore.c, tm_dinput8.c): mounts in listings and attributes, path
 * translation and case, mount overlap, writes, GetFullPathNameA, and the
 * four separate questions about a DLL: is the file there, does it load, is
 * the export there, is the export implemented. Synthetic directories only.
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
#include "tm_wmvcore.h"
#include "tm_dinput8.h"

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
    FILE *f = fopen(p, "w"); if (f) { fputs("x", f); fclose(f); }
    hle_path_changed();
}
static void mkd(const char *dir, const char *rel) {
    char p[PATH_MAX * 2]; snprintf(p, sizeof p, "%s/%s", dir, rel); mkdir(p, 0755); hle_path_changed();
}
static int exists(const char *dir, const char *rel) {
    char p[PATH_MAX * 2]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, rel); return stat(p, &st) == 0;
}

static char *g_str, *g_fd;
static uint16_t *g_w, *g_fdw;
static uint32_t ws(const char *s) {                    /* a UTF-16 copy of an ASCII or Latin-1 string */
    size_t i = 0;
    for (; s[i]; i++) g_w[i] = (uint8_t)s[i];
    g_w[i] = 0;
    return hle_va(g_w);
}
static int wide_is(const uint16_t *w, const char *s) {
    for (size_t i = 0;; i++) { if (w[i] != (uint8_t)s[i]) return 0; if (!s[i]) return 1; }
}
static uint32_t gs(const char *s) { strcpy(g_str, s); return hle_va(g_str); }
static uint32_t call(const char *name, int n, const uint32_t *args) { return hle_call_guest(hle_resolve(name), n, args); }

/* A listing as "name/ name ..." (a '/' after a directory), in the order given. */
static int list(const char *pattern, char *out, size_t n) {
    out[0] = 0;
    uint32_t a[2] = { gs(pattern), hle_va(g_fd) };
    uint32_t h = call("tm:FindFirstFileA", 2, a);
    if (h == 0xFFFFFFFFu) return -1;
    int count = 0;
    do {
        size_t k = strlen(out);
        snprintf(out + k, n - k, "%s%s%s", k ? " " : "", g_fd + 44, (*(uint32_t *)g_fd & 0x10) ? "/" : "");
        count++;
        uint32_t b[2] = { h, hle_va(g_fd) };
        if (!call("tm:FindNextFileA", 2, b)) break;
    } while (1);
    uint32_t c[1] = { h };
    call("FindClose", 1, c);
    return count;
}
static uint32_t attrs(const char *p) { uint32_t a[1] = { gs(p) }; return call("tm:GetFileAttributesA", 1, a); }
static int listed(const char *pattern, const char *want) {
    char l[4096];
    int n = list(pattern, l, sizeof l);
    if (n < 0 || strcmp(l, want)) fprintf(stderr, "  %s -> [%s] (want [%s])\n", pattern, n < 0 ? "not found" : l, want);
    return n >= 0 && !strcmp(l, want);
}
static int host_is(const char *guest, const char *dir, const char *rel) {
    char h[1024], want[PATH_MAX * 2];
    hle_host_path(guest, h, sizeof h);
    snprintf(want, sizeof want, "%s%s%s", dir, *rel ? "/" : "", rel);
    if (strcmp(h, want)) fprintf(stderr, "  %s -> %s (want %s)\n", guest, h, want);
    return !strcmp(h, want);
}
static int full_is(const char *in, const char *want, const char *part) {
    static char *buf; if (!buf) buf = hle_alloc(512);
    static uint32_t *fp; if (!fp) fp = (uint32_t *)hle_alloc(4);
    uint32_t a[4] = { gs(in), 512, hle_va(buf), hle_va(fp) };
    *fp = 0xDEADu;
    uint32_t n = call("tm:GetFullPathNameA", 4, a);
    int ok = n == strlen(want) && !strcmp(buf, want) &&
             (part ? *fp && !strcmp((char *)hle_ptr(*fp), part) : *fp == 0);
    if (!ok) fprintf(stderr, "  GetFullPathNameA(%s) -> %u %s (want %s)\n", in, n, buf, want);
    return ok;
}

/* a host DLL with one implemented and one unimplemented export, and one
 * that is only a file entry */
static int g_attach, g_detach;
static int on_load(void) { g_attach++; return 1; }
static void on_unload(void) { g_detach++; }
static void k_Impl(void) { RET(0x2222u, 0); }
static const win32hle_shim t_shims[] = { { "vtest.dll!Impl", k_Impl }, { 0, 0 } };
static const tm_host_export t_exports[] = {
    { "Impl", "vtest.dll!Impl", NULL }, { "Later", NULL, "a test export with no implementation" }, { 0, 0, 0 } };
static const tm_host_dll t_dll = { .name = "vtest.dll", .visible = 1, .loadable = 1, .exports = t_exports,
                                   .on_load = on_load, .on_unload = on_unload };
static const tm_host_dll t_fileonly = { .name = "fileonly.dll", .visible = 1, .loadable = 0, .exports = t_exports + 2 };
static int fail_load(void) { return 0; }
static const tm_host_dll t_badinit = { .name = "badinit.dll", .visible = 0, .loadable = 1, .exports = t_exports + 2,
                                       .on_load = fail_load };

static uint32_t load(const char *n) { uint32_t a[1] = { gs(n) }; return call("tm:LoadLibraryA", 1, a); }
static uint32_t freelib(uint32_t h) { uint32_t a[1] = { h }; return call("tm:FreeLibrary", 1, a); }
static uint32_t proc(uint32_t h, const char *n) {
    static char *pn; if (!pn) pn = hle_alloc(64);
    uint32_t a[2] = { h, (strcpy(pn, n), hle_va(pn)) };
    return call("tm:GetProcAddress", 2, a);
}
static int file_there(const char *p) {
    uint32_t a[2] = { gs(p), hle_va(g_fd) };
    uint32_t h = call("tm:FindFirstFileA", 2, a);
    if (h == 0xFFFFFFFFu) return 0;
    uint32_t c[1] = { h }; call("FindClose", 1, c);
    return 1;
}

int main(void) {
    char base[] = "/tmp/tm-vfs-XXXXXX";
    if (!mkdtemp(base)) return 1;
    snprintf(G, sizeof G, "%s/game", base); snprintf(S, sizeof S, "%s/system", base); snprintf(U, sizeof U, "%s/user", base);
    mkdir(G, 0755); mkdir(U, 0755);
    touch(G, "Movies.exe"); mkd(G, "Data"); touch(G, "Data/a.ini"); touch(G, "Data/B.INI"); touch(G, "Data/noext");

    recomp_host_init();
    hle_set_drive('c', G);
    hle_set_drive('d', U);
    win32hle_register(t_shims);
    win32hle_register(tm_vfs_overrides);
    win32hle_register(tm_sysroot_overrides);
    win32hle_register(tm_loader_overrides);
    tm_dinput8_init();
    tm_wmvcore_init();
    tm_loader_add(&t_dll);
    tm_loader_add(&t_fileonly);
    tm_loader_add(&t_badinit);
    tm_loader_set_app_dir("C:\\Movies.exe");
    if (chdir(G)) return 1;                          /* the game's current directory, C:\ */
    check("tm_sysroot_init", tm_sysroot_init(G, S, U));
    mach_enter();
    g_str = hle_alloc(1024);
    g_fd = hle_alloc(320);
    g_w = hle_alloc(2048);
    g_fdw = hle_alloc(592);

    /* root enumeration: the mount is an entry of C:\, no "." / ".." in a root */
    check("C:\\* lists WINDOWS with the game's entries", listed("C:\\*", "Data/ Movies.exe WINDOWS/"));
    check("C:\\*.* the same", listed("C:\\*.*", "Data/ Movies.exe WINDOWS/"));
    check("c:/w* (case, '/')", listed("c:/w*", "WINDOWS/"));
    check("relative * from C:\\", listed("*", "Data/ Movies.exe WINDOWS/"));
    check("C:\\WINDOWS without a wildcard: the entry, by its guest name", listed("C:\\windows", "WINDOWS/"));
    /* a real directory of that name in the game folder is hidden, not listed twice */
    mkd(G, "windows");
    check("a real C:\\windows folder: one WINDOWS entry", listed("C:\\*", "Data/ Movies.exe WINDOWS/"));
    check("  and C:\\WINDOWS still resolves to the system directory", host_is("C:\\Windows", S, ""));
    rmdir(strcat(strcpy(g_str + 512, G), "/windows")); hle_path_changed();

    /* nested enumeration */
    check("C:\\WINDOWS\\*: the system directory's entries", listed("C:\\WINDOWS\\*", "./ ../ SYSTEM32/"));
    check("SYSTEM32\\*: the visible host DLLs", listed("C:\\WINDOWS\\SYSTEM32\\*", "./ ../ dinput8.dll fileonly.dll vtest.dll wmvcore.dll"));
    touch(S, "SYSTEM32/real.dll");
    touch(S, "SYSTEM32/WMVCORE.DLL");                 /* a real file of a host DLL's name, any case */
    check("real and virtual merged, no duplicate", listed("C:\\WINDOWS\\SYSTEM32\\*.dll", "dinput8.dll fileonly.dll real.dll vtest.dll wmvcore.dll"));
    check("a mask matches virtual entries", listed("C:\\WINDOWS\\SYSTEM32\\WMV*", "wmvcore.dll"));
    char p[PATH_MAX * 2];
    snprintf(p, sizeof p, "%s/SYSTEM32/WMVCORE.DLL", S); unlink(p); hle_path_changed();
    check("Data\\*.ini (relative, any case)", listed("Data\\*.ini", "a.ini B.INI"));
    check("Data\\*. : names without an extension", listed("C:\\Data\\*.", "./ ../ noext"));
    check("missing directory: ERROR_PATH_NOT_FOUND", list("C:\\nope\\*", p, sizeof p) < 0 && hle_get_last_error() == 3);
    check("missing name: ERROR_FILE_NOT_FOUND", list("C:\\Data\\zz", p, sizeof p) < 0 && hle_get_last_error() == 2);
    uint32_t bad[2] = { 0x12345u, hle_va(g_fd) };
    check("FindNextFileA on a bad handle: ERROR_INVALID_HANDLE", call("tm:FindNextFileA", 2, bad) == 0 && hle_get_last_error() == 6);

    /* attributes */
    check("C:\\WINDOWS is a directory", attrs("C:\\WINDOWS") == 0x10);
    check("c:\\windows\\system32 is a directory", attrs("c:\\windows\\system32") == 0x10);
    check("SYSTEM32\\wmvcore.dll: a file (archive)", attrs("C:\\WINDOWS\\SYSTEM32\\wmvcore.dll") == 0x20);
    check("SYSTEM32\\DINPUT8.DLL any case", attrs("C:\\WINDOWS\\SYSTEM32\\DINPUT8.DLL") == 0x20);
    check("a host DLL is not in another directory", attrs("C:\\wmvcore.dll") == 0xFFFFFFFFu);
    check("missing: INVALID_FILE_ATTRIBUTES", attrs("C:\\WINDOWS\\SYSTEM32\\nosuch.dll") == 0xFFFFFFFFu);
    uint32_t fa[2] = { gs("C:\\WINDOWS\\SYSTEM32\\wmvcore.dll"), hle_va(g_fd) };
    uint32_t fh = call("tm:FindFirstFileA", 2, fa);
    check("FindFirstFileA(SYSTEM32\\wmvcore.dll): found, a file, size 0", fh != 0xFFFFFFFFu && !strcmp(g_fd + 44, "wmvcore.dll") &&
          *(uint32_t *)g_fd == 0x20 && *(uint32_t *)(g_fd + 32) == 0);
    if (fh != 0xFFFFFFFFu) { uint32_t c[1] = { fh }; call("FindClose", 1, c); }

    /* path translation: made absolute and normalized before the mount is matched */
    check("C:\\Data\\..\\WINDOWS\\SYSTEM32 -> system", host_is("C:\\Data\\..\\WINDOWS\\SYSTEM32", S, "SYSTEM32"));
    check("C:\\WINDOWS\\..\\Movies.exe -> game", host_is("C:\\WINDOWS\\..\\Movies.exe", G, "Movies.exe"));
    check("relative WINDOWS\\SYSTEM32 from C:\\", host_is("WINDOWS\\SYSTEM32", S, "SYSTEM32"));
    check("C:windows (drive-relative)", host_is("C:windows", S, ""));
    check("\\WINDOWS (rooted)", host_is("\\WINDOWS", S, ""));
    check("C:\\..\\..\\WINDOWS stops at the root", host_is("C:\\..\\..\\WINDOWS", S, ""));
    check("C:\\WINDOWSX is not the mount", host_is("C:\\WINDOWSX", G, "WINDOWSX"));

    /* GetFullPathNameA */
    check("full path of a relative name", full_is("data\\a.ini", "C:\\data\\a.ini", "a.ini"));
    check("full path: .. and .", full_is("C:\\WINDOWS\\.\\SYSTEM32\\..\\x.dll", "C:\\WINDOWS\\x.dll", "x.dll"));
    check("full path: trailing separator kept, no file part", full_is("C:/Data/", "C:\\Data\\", NULL));
    check("full path: trailing dots and spaces go", full_is("C:\\Data\\file. .", "C:\\Data\\file", "file"));
    static char *small; if (!small) small = hle_alloc(16);
    strcpy(small, "untouched");
    uint32_t sm[4] = { gs("C:\\Data\\a.ini"), 5, hle_va(small), 0 };
    check("full path, small buffer: size needed, untouched", call("tm:GetFullPathNameA", 4, sm) == 14 && !strcmp(small, "untouched"));
    if (chdir(S)) return 1;                           /* SetCurrentDirectory into C:\WINDOWS */
    check("relative from C:\\WINDOWS", full_is("SYSTEM32\\..\\..\\Movies.exe", "C:\\Movies.exe", "Movies.exe"));
    check("  and it opens the game's file", host_is("..\\Movies.exe", G, "Movies.exe"));
    if (chdir(G)) return 1;

    /* writes stay out of the game */
    uint32_t cd[2] = { gs("C:\\Data\\..\\WINDOWS\\TEMP"), 0 };
    check("CreateDirectoryA through ..: made in the system directory", call("tm:CreateDirectoryA", 2, cd) == 1 && exists(S, "TEMP"));
    check("  not in the game directory", !exists(G, "WINDOWS") && !exists(G, "windows"));
    uint32_t cf[7] = { gs("C:\\WINDOWS\\TEMP\\t.txt"), 0x40000000u, 0, 0, 2, 0x80, 0 };
    uint32_t hf = call("tm:CreateFileA", 7, cf);
    check("CreateFileA under C:\\WINDOWS: in the system directory", hf != 0xFFFFFFFFu && exists(S, "TEMP/t.txt"));
    if (hf != 0xFFFFFFFFu) { uint32_t c[1] = { hf }; call("CloseHandle", 1, c); }
    uint32_t cv[7] = { gs("C:\\WINDOWS\\SYSTEM32\\wmvcore.dll"), 0x80000000u, 1, 0, 3, 0x80, 0 };
    check("CreateFileA of a host DLL entry: ERROR_ACCESS_DENIED (no contents)",
          call("tm:CreateFileA", 7, cv) == 0xFFFFFFFFu && hle_get_last_error() == 5);
    cv[1] = 0x40000000u, cv[4] = 2;
    check("  nor can it be replaced", call("tm:CreateFileA", 7, cv) == 0xFFFFFFFFu && !exists(S, "SYSTEM32/wmvcore.dll"));
    uint32_t df[1] = { gs("C:\\WINDOWS\\SYSTEM32\\dinput8.dll") };
    check("DeleteFileA of a host DLL entry: ERROR_ACCESS_DENIED", call("tm:DeleteFileA", 1, df) == 0 && hle_get_last_error() == 5);

    /* mount overlap rejection */
    char why[1024], other[PATH_MAX * 2];
    snprintf(other, sizeof other, "%s/other", base); mkdir(other, 0755);
    check("a mount inside C:\\WINDOWS: refused", !tm_vfs_mount("C:\\WINDOWS\\SYSTEM32", other, why, sizeof why));
    check("a mount around it: refused", !tm_vfs_mount("C:\\", other, why, sizeof why));
    check("C:\\windows again, other case: refused", !tm_vfs_mount("c:\\windows", other, why, sizeof why));
    check("the same host directory elsewhere: refused", !tm_vfs_mount("C:\\Other", S, why, sizeof why));
    snprintf(p, sizeof p, "%s/SYSTEM32", S);
    check("a host directory inside another mount's: refused", !tm_vfs_mount("C:\\Other", p, why, sizeof why));
    check("a missing host directory: refused", !tm_vfs_mount("C:\\Other", "/nonexistent-tm-vfs", why, sizeof why));

    /* the four questions about a DLL */
    check("1. file: wmvcore.dll is in SYSTEM32", file_there("C:\\WINDOWS\\SYSTEM32\\wmvcore.dll"));
    check("1. file: fileonly.dll is in SYSTEM32", file_there("C:\\WINDOWS\\SYSTEM32\\fileonly.dll"));
    check("1. file: badinit.dll is not listed", !file_there("C:\\WINDOWS\\SYSTEM32\\badinit.dll"));
    hle_set_last_error(0);
    check("2. module: fileonly.dll does not load (ERROR_BAD_EXE_FORMAT)", load("fileonly.dll") == 0 && hle_get_last_error() == 193);
    hle_set_last_error(0);
    check("2. module: an attach failure (ERROR_DLL_INIT_FAILED), not loaded",
          load("badinit.dll") == 0 && hle_get_last_error() == 1114 && tm_loader_refs("badinit.dll") == 0);
    uint32_t hv = load("vtest");
    check("2. module: vtest loads, attach once", hv && g_attach == 1 && tm_loader_refs("vtest.dll") == 1);
    check("2. module: by its full path, same handle", load("C:\\WINDOWS\\SYSTEM32\\VTEST.DLL") == hv && g_attach == 1 &&
          tm_loader_refs("vtest.dll") == 2);
    check("2. module: through a .. path too", load("C:\\Data\\..\\WINDOWS\\System32\\vtest.dll") == hv && tm_loader_refs("vtest.dll") == 3);
    check("3. export: Impl and Later exist", tm_loader_export("vtest.dll", "Impl") != TM_EXPORT_NONE &&
          tm_loader_export("vtest.dll", "Later") != TM_EXPORT_NONE);
    check("3. export: Other does not", tm_loader_export("vtest.dll", "Other") == TM_EXPORT_NONE);
    check("4. implemented: Impl", tm_loader_export("vtest.dll", "Impl") == TM_EXPORT_IMPLEMENTED);
    uint32_t e = proc(hv, "Impl");
    check("4. GetProcAddress(Impl): runs", e && hle_call_guest(e, 0, NULL) == 0x2222u);
    hle_set_last_error(0);
    check("4. not implemented: Later -> NULL, ERROR_PROC_NOT_FOUND",
          tm_loader_export("vtest.dll", "Later") == TM_EXPORT_UNSUPPORTED && proc(hv, "Later") == 0 && hle_get_last_error() == 127);
    hle_set_last_error(0);
    check("3. not an export: Other -> NULL, ERROR_PROC_NOT_FOUND", proc(hv, "Other") == 0 && hle_get_last_error() == 127);
    freelib(hv); freelib(hv);
    check("refs: still loaded at 1, no detach", tm_loader_refs("vtest.dll") == 1 && g_detach == 0);
    freelib(hv);
    check("refs: unloaded at 0, detach once", tm_loader_refs("vtest.dll") == 0 && g_detach == 1);
    check("  and listed still (a file entry is not a module)", file_there("C:\\WINDOWS\\SYSTEM32\\vtest.dll"));

    /* wmvcore.dll: present and loadable; neither export implemented */
    uint32_t hw = load("WMVCore.DLL");
    check("wmvcore.dll loads as a host module", hw >= 0x7D000000u && tm_loader_refs("wmvcore.dll") == 1);
    check("WMCreateProfileManager: an export, not implemented",
          tm_loader_export("wmvcore.dll", "WMCreateProfileManager") == TM_EXPORT_UNSUPPORTED && proc(hw, "WMCreateProfileManager") == 0 &&
          hle_get_last_error() == 127);
    check("WMCreateSyncReader: an export, not implemented",
          tm_loader_export("wmvcore.dll", "WMCreateSyncReader") == TM_EXPORT_UNSUPPORTED && proc(hw, "WMCreateSyncReader") == 0);
    check("WMCreateReader: not an export of this host DLL", tm_loader_export("wmvcore.dll", "WMCreateReader") == TM_EXPORT_NONE);
    check("FreeLibrary(wmvcore)", freelib(hw) == 1 && tm_loader_refs("wmvcore.dll") == 0);
    uint32_t hd = load("dinput8.dll");
    check("dinput8.dll: listed, loads, DirectInput8Create implemented", file_there("C:\\WINDOWS\\SYSTEM32\\dinput8.dll") && hd &&
          proc(hd, "DirectInput8Create") != 0 && tm_loader_export("dinput8.dll", "DirectInput8Create") == TM_EXPORT_IMPLEMENTED);
    freelib(hd);

    /* ---- the W functions: the ANSI overrides, names converted ---- */
    uint32_t wf[2] = { ws("C:\\Data\\*.ini"), hle_va(g_fdw) };
    uint32_t wh = call("tm:FindFirstFileW", 2, wf);
    check("FindFirstFileW(C:\\Data\\*.ini): a.ini first, as the ANSI listing", wh != 0xFFFFFFFFu && wide_is(g_fdw + 22, "a.ini"));
    uint32_t wn[2] = { wh, hle_va(g_fdw) };
    check("  FindNextFileW: B.INI", call("tm:FindNextFileW", 2, wn) == 1 && wide_is(g_fdw + 22, "B.INI"));
    check("  then ERROR_NO_MORE_FILES", call("tm:FindNextFileW", 2, wn) == 0 && hle_get_last_error() == 18);
    uint32_t wc[1] = { wh };
    check("  FindClose on the W handle", call("tm:FindClose", 1, wc) == 1);
    uint32_t wsys[2] = { ws("c:\\windows\\system32\\WMVCORE.DLL"), hle_va(g_fdw) };
    wh = call("tm:FindFirstFileW", 2, wsys);
    check("FindFirstFileW of a host DLL: found, archive, size 0, its listed name",
          wh != 0xFFFFFFFFu && *(uint32_t *)g_fdw == 0x20 && *(uint32_t *)((char *)g_fdw + 32) == 0 && wide_is(g_fdw + 22, "wmvcore.dll"));
    if (wh != 0xFFFFFFFFu) { wc[0] = wh; call("tm:FindClose", 1, wc); }
    uint32_t wm[2] = { ws("C:\\nope\\*"), hle_va(g_fdw) };
    check("FindFirstFileW, missing directory: ERROR_PATH_NOT_FOUND", call("tm:FindFirstFileW", 2, wm) == 0xFFFFFFFFu && hle_get_last_error() == 3);
    uint32_t wnull[2] = { 0, hle_va(g_fdw) };
    check("FindFirstFileW(NULL): ERROR_INVALID_PARAMETER", call("tm:FindFirstFileW", 2, wnull) == 0xFFFFFFFFu && hle_get_last_error() == 87);
    uint32_t wa[1] = { ws("C:\\WINDOWS") };
    check("GetFileAttributesW(C:\\WINDOWS): a directory", call("tm:GetFileAttributesW", 1, wa) == 0x10);
    wa[0] = ws("C:\\WINDOWS\\SYSTEM32\\dinput8.dll");
    check("GetFileAttributesW of a host DLL: archive", call("tm:GetFileAttributesW", 1, wa) == 0x20);
    uint32_t wcr[7] = { ws("C:\\WINDOWS\\SYSTEM32\\dinput8.dll"), 0x80000000u, 1, 0, 3, 0x80, 0 };
    check("CreateFileW of a host DLL: ERROR_ACCESS_DENIED, as CreateFileA", call("tm:CreateFileW", 7, wcr) == 0xFFFFFFFFu && hle_get_last_error() == 5);
    uint32_t wcd[2] = { ws("D:\\WideDir"), 0 };
    check("CreateDirectoryW in the user directory: made", call("tm:CreateDirectoryW", 2, wcd) == 1 && exists(U, "WideDir"));
    uint32_t wnew[7] = { ws("D:\\WideDir\\f.txt"), 0x40000000u, 0, 0, 2, 0x80, 0 };
    uint32_t fhw = call("tm:CreateFileW", 7, wnew);
    check("CreateFileW(CREATE_ALWAYS) in it: a handle, the file on the host", fhw != 0xFFFFFFFFu && exists(U, "WideDir/f.txt"));
    if (fhw != 0xFFFFFFFFu) { uint32_t c[1] = { fhw }; call("CloseHandle", 1, c); }
    uint32_t wdel[1] = { ws("D:\\WideDir\\f.txt") };
    check("DeleteFileW: gone", call("tm:DeleteFileW", 1, wdel) == 1 && !exists(U, "WideDir/f.txt"));
    uint32_t wfp[4] = { ws("data\\a.ini"), 260, hle_va(g_fdw), hle_va(g_fd) };
    uint32_t wl = call("tm:GetFullPathNameW", 4, wfp);
    check("GetFullPathNameW(data\\a.ini): C:\\data\\a.ini, its length, the file part in the wide buffer",
          wl == 13 && wide_is(g_fdw, "C:\\data\\a.ini") && *(uint32_t *)g_fd == hle_va(g_fdw) + 2u * 8);
    uint32_t wsmall[4] = { ws("data\\a.ini"), 4, hle_va(g_fdw), 0 };
    check("  a buffer too small: the size needed, with its terminator", call("tm:GetFullPathNameW", 4, wsmall) == 14);
    mach_leave();

    check("the game directory is unchanged", !exists(G, "WINDOWS") && !exists(G, "windows") && !exists(G, "TEMP"));
    snprintf(p, sizeof p, "rm -rf '%s'", base);
    if (system(p)) return 1;
    printf("vfs: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
