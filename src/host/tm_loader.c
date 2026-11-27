/*
 * tm_loader.c - LoadLibraryA, GetModuleHandleA, GetProcAddress, FreeLibrary
 * and GetModuleFileNameA in front of win32hle's (docs/IMPORTS.md, "DLL
 * loading" and "Host DLLs"). Movies.exe imports only these ANSI forms: no
 * LoadLibraryEx, no SetDllDirectory, and it has no manifest.
 *
 * Two kinds of module are answered here:
 *   - host DLLs (tm_host_dll): system DLLs this host implements itself,
 *     such as dinput8.dll. Each has a directory (the system directory), may
 *     be visible there to file queries (tm_vfs.c lists it; nothing is on
 *     disk), may be loadable, and lists its exports, each implemented or
 *     not. Handles are pseudo-modules (no image) at 0x7D000000 + 0x10000 *
 *     index. The reference count follows LoadLibrary/FreeLibrary: on_load
 *     runs at the first reference, on_unload at the last FreeLibrary, and
 *     the handle stops working. The handle range is reserved in guest
 *     memory and made inaccessible (reserve_handles), so no guest allocation
 *     can be handed out there and code that reads a handle as an image
 *     faults at once, naming the address.
 *   - everything else goes to win32hle: its own system DLLs (kernel32,
 *     user32, ...) and DLL files, which it maps without running their code.
 *     Its system DLLs are pseudo-modules at 0x7E000000 + 0x10000 * k. That
 *     range, to the shim range at 0x7F000000, is reserved here as well and
 *     made inaccessible. win32hle never unloads a module (its FreeLibrary
 *     keeps the count at 1 or more), so its handles never go stale. Its
 *     FreeLibrary and GetProcAddress do not check the handle: a handle that
 *     names no module is refused here first (ERROR_MOD_NOT_FOUND).
 *
 * The search for a name without a path is that of LoadLibraryA on Windows
 * XP SP2 and later with SafeDllSearchMode on (its default), no
 * SetDllDirectory and no activation context:
 *   0. a module of that name already loaded (base name, any case);
 *   1. the directory of the application;
 *   2. the system directory, C:\WINDOWS\SYSTEM32;
 *   3. the 16-bit system directory, C:\WINDOWS\SYSTEM;
 *   4. the Windows directory, C:\WINDOWS;
 *   5. the current directory, 6. PATH: win32hle's search (it tries the
 *      current directory, then the application directory again; the
 *      guest has no PATH).
 * In 2-4 a host DLL of that directory is taken before a file there. Steps
 * 1-4 are done here, before win32hle sees the name, because win32hle's own
 * order puts the current directory first. A name with a path is loaded
 * from that path only (made absolute the Windows way first). ".dll" is
 * appended to a name with no extension, and a trailing "." means "no
 * extension". Not modelled: KnownDLLs (the install has no DLL of a system
 * name), .local redirection and side-by-side assemblies (no manifest), and
 * the imports of a host DLL (it has none).
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include "win32hle.h"
#include "tm_loader.h"

#define ERROR_MOD_NOT_FOUND       126u
#define ERROR_PROC_NOT_FOUND      127u
#define ERROR_BAD_EXE_FORMAT      193u
#define ERROR_DLL_INIT_FAILED    1114u
#define ERROR_INSUFFICIENT_BUFFER 122u
#define HOST_DLL_BASE  0x7D000000u
#define HOST_DLL_STEP  0x00010000u
#define SYS_MOD_BASE   0x7E000000u          /* win32hle module.c: system pseudo-modules */
#define SYS_MOD_END    0x7F000000u          /* the shim range starts here */
#define TM_MAX_HOST_DLLS TM_LOADER_MAX_DLLS
#define TM_MAX_ASKED 32

int hle_guest_full_path(const char *in, char *out, size_t n);   /* patches/pcrecomp/0001 */

static const tm_host_dll *g_dll[TM_MAX_HOST_DLLS];
static int g_refs[TM_MAX_HOST_DLLS], g_loads[TM_MAX_HOST_DLLS];
static int g_ndll;
static char g_app_dir[600];                 /* guest, no trailing backslash */
static struct { int dll; char name[64]; tm_export_state st; int n; } g_asked[TM_MAX_ASKED];
static int g_nasked;

/* Keep a range of module handles out of the guest allocator (both lie
 * inside its "anywhere" range, which is first fit from 0x10000000) and
 * inaccessible, so a handle is never also a guest address. */
static void reserve_range(const char *what, uint32_t va, uint32_t size) {
    uint32_t lo = hle_vm_reserve(va, size);
    if (lo != va) hle_fatal("%s: guest range 0x%08X..0x%08X already in use", what, va, va + size);
    if (mprotect(hle_ptr(lo), size, PROT_NONE)) hle_fatal("%s: cannot protect 0x%08X", what, lo);
}
void tm_loader_reserve(void) {
    static int done;
    if (done) return;
    done = 1;
    reserve_range("host DLL handles", HOST_DLL_BASE, HOST_DLL_STEP * TM_MAX_HOST_DLLS);
    reserve_range("system module handles", SYS_MOD_BASE, SYS_MOD_END - SYS_MOD_BASE);
}

int tm_loader_add(const tm_host_dll *dll) {
    tm_loader_reserve();
    for (int i = 0; i < g_ndll; i++)
        if (!strcmp(g_dll[i]->name, dll->name)) {
            fprintf(stderr, "[loader] host DLL %s registered twice\n", dll->name);
            return 0;
        }
    if (g_ndll == TM_MAX_HOST_DLLS) {
        fprintf(stderr, "[loader] host DLL %s not registered: the table holds %d\n", dll->name, TM_MAX_HOST_DLLS);
        return 0;
    }
    g_dll[g_ndll++] = dll;
    return 1;
}
int tm_loader_count(void) { return g_ndll; }
const tm_host_dll *tm_loader_dll(int i) { return i >= 0 && i < g_ndll ? g_dll[i] : NULL; }
const char *tm_loader_dir(const tm_host_dll *dll) { return dll->dir ? dll->dir : TM_SYSTEM_DIR; }

void tm_loader_set_app_dir(const char *guest_exe_path) {
    snprintf(g_app_dir, sizeof g_app_dir, "%s", guest_exe_path);
    char *bs = strrchr(g_app_dir, '\\');
    if (bs) *bs = 0; else g_app_dir[0] = 0;
}

/* The name's file part, lower case, with ".dll" added when it has no
 * extension (a trailing '.' is dropped and means none). */
static void module_name(const char *path, char *out, size_t n) {
    const char *b = path + strlen(path);
    while (b > path && b[-1] != '\\' && b[-1] != '/' && b[-1] != ':') b--;
    size_t k = 0;
    for (; b[k] && k + 1 < n; k++) out[k] = (char)tolower((unsigned char)b[k]);
    out[k] = 0;
    if (k && out[k - 1] == '.') out[k - 1] = 0;
    else if (!strchr(out, '.') && k + 5 < n) strcat(out, ".dll");
}

static int has_path(const char *name) { return strchr(name, '\\') || strchr(name, '/') || strchr(name, ':'); }

static int host_dll_index(const char *name) {
    char m[96];
    module_name(name, m, sizeof m);
    for (int i = 0; i < g_ndll; i++) if (!strcmp(g_dll[i]->name, m)) return i;
    return -1;
}

const tm_host_dll *tm_loader_visible(const char *dir, const char *name) {
    for (int i = 0; i < g_ndll; i++)
        if (g_dll[i]->visible && !strcasecmp(tm_loader_dir(g_dll[i]), dir) && !strcasecmp(g_dll[i]->name, name))
            return g_dll[i];
    return NULL;
}

/* A path names host DLL i: its directory (made absolute) and its name. */
static int path_is_dll(const char *path, int i) {
    char full[1024];
    if (!hle_guest_full_path(path, full, sizeof full)) return 0;
    char *bs = strrchr(full, '\\');
    if (!bs) return 0;
    *bs = 0;
    char m[96];
    module_name(bs + 1, m, sizeof m);
    const char *d = tm_loader_dir(g_dll[i]);
    return !strcasecmp(full, d) && !strcmp(m, g_dll[i]->name);
}

static int handle_index(uint32_t h) {
    if (h < HOST_DLL_BASE || (h - HOST_DLL_BASE) % HOST_DLL_STEP) return -1;
    uint32_t i = (h - HOST_DLL_BASE) / HOST_DLL_STEP;
    return i < (uint32_t)g_ndll && g_refs[i] > 0 ? (int)i : -1;
}
static uint32_t handle_of(int i) { return HOST_DLL_BASE + HOST_DLL_STEP * (uint32_t)i; }

int tm_loader_refs(const char *name) { int i = host_dll_index(name); return i < 0 ? -1 : g_refs[i]; }

tm_export_state tm_loader_export(const char *dll, const char *export_name) {
    int i = host_dll_index(dll);
    if (i < 0 || !export_name) return TM_EXPORT_NONE;
    for (const tm_host_export *e = g_dll[i]->exports; e && e->name; e++)
        if (!strcmp(e->name, export_name))                       /* export names are case-sensitive */
            return e->shim && hle_resolve(e->shim) ? TM_EXPORT_IMPLEMENTED : TM_EXPORT_UNSUPPORTED;
    return TM_EXPORT_NONE;
}

/* A file name in a guest directory, if it is there (found without case). */
static int file_in(const char *dir, const char *m, char *guest_out, size_t n) {
    char g[1024], host[1024];
    snprintf(g, sizeof g, "%s\\%s", dir, m);
    if (!hle_host_path(g, host, sizeof host)) return 0;
    snprintf(guest_out, n, "%s", g);
    return 1;
}

tm_load_where tm_loader_locate(const char *name, char *guest_out, size_t n) {
    guest_out[0] = 0;
    if (!name || !name[0]) return TM_LOAD_NOT_FOUND;
    int i = host_dll_index(name);
    if (!has_path(name)) {
        char m[96];
        module_name(name, m, sizeof m);
        if (i >= 0 && g_refs[i] > 0) return TM_LOAD_HOST_DLL;                  /* 0. loaded */
        if (g_app_dir[0] && file_in(g_app_dir, m, guest_out, n)) return TM_LOAD_FILE;   /* 1. */
        static const char *const dirs[] = { TM_SYSTEM_DIR, TM_SYSTEM16_DIR, TM_WINDOWS_DIR };   /* 2, 3, 4 */
        for (int k = 0; k < 3; k++) {
            if (i >= 0 && !strcasecmp(tm_loader_dir(g_dll[i]), dirs[k])) return TM_LOAD_HOST_DLL;
            if (file_in(dirs[k], m, guest_out, n)) return TM_LOAD_FILE;
        }
        return TM_LOAD_WIN32HLE;                                               /* 2 (its DLLs), 5, 6 */
    }
    if (i >= 0 && path_is_dll(name, i)) return TM_LOAD_HOST_DLL;
    return TM_LOAD_WIN32HLE;                                                   /* a path: only there */
}

static void asked(int dll, const char *name, tm_export_state st) {
    for (int k = 0; k < g_nasked; k++)
        if (g_asked[k].dll == dll && !strcmp(g_asked[k].name, name)) { g_asked[k].n++; return; }
    if (g_nasked < TM_MAX_ASKED) {
        g_asked[g_nasked].dll = dll, g_asked[g_nasked].st = st, g_asked[g_nasked].n = 1;
        snprintf(g_asked[g_nasked].name, sizeof g_asked[0].name, "%s", name);
        g_nasked++;
    }
}

void tm_loader_report(FILE *out) {
    for (int i = 0; i < g_ndll; i++)
        if (g_loads[i]) fprintf(out, "[loader] host DLL %s: loaded %d time(s), refs at exit %d\n", g_dll[i]->name, g_loads[i], g_refs[i]);
    static const char *const st[] = { "not an export", "export not implemented", "implemented" };
    for (int k = 0; k < g_nasked; k++)
        fprintf(out, "[loader] GetProcAddress(%s, \"%s\") x%d: %s\n", g_dll[g_asked[k].dll]->name, g_asked[k].name,
                g_asked[k].n, st[g_asked[k].st]);
}

/* Run win32hle's shim of this name on the current guest frame. */
static void delegate(const char *name) {
    recomp_func_t f = recomp_lookup_import(hle_resolve(name));
    f();
}

/* Whether win32hle has a module with this handle. Its GetModuleFileNameA
 * looks the handle up first and sets ERROR_MOD_NOT_FOUND only when it
 * names no module; with no buffer it then returns 0 without touching the
 * error. Called that way it allocates nothing in guest memory (a scratch
 * buffer would move every later guest allocation). The last error is left
 * as it was. */
int tm_loader_win32hle_module(uint32_t h) {
    if (!h) return 0;
    uint32_t err = hle_get_last_error();
    hle_set_last_error(0);
    uint32_t args[3] = { h, 0, 0 };
    hle_call_guest(hle_resolve("GetModuleFileNameA"), 3, args);
    int known = hle_get_last_error() != ERROR_MOD_NOT_FOUND;
    hle_set_last_error(err);
    return known;
}

static void k_LoadLibraryA(void) {                      /* (name) */
    const char *name = ASTR(0);
    char g[1024];
    tm_load_where w = name ? tm_loader_locate(name, g, sizeof g) : TM_LOAD_NOT_FOUND;
    if (w == TM_LOAD_HOST_DLL) {
        int i = host_dll_index(name);
        if (!g_dll[i]->loadable) {
            fprintf(stderr, "[loader] LoadLibraryA(\"%s\"): host DLL %s is a file entry only, not a loadable module\n", name, g_dll[i]->name);
            hle_set_last_error(ERROR_BAD_EXE_FORMAT);
            RET(0, 1);
        }
        if (g_refs[i] == 0 && g_dll[i]->on_load && !g_dll[i]->on_load()) {
            fprintf(stderr, "[loader] LoadLibraryA(\"%s\"): host DLL %s failed to initialize\n", name, g_dll[i]->name);
            hle_set_last_error(ERROR_DLL_INIT_FAILED);
            RET(0, 1);
        }
        g_refs[i]++, g_loads[i]++;
        fprintf(stderr, "[loader] LoadLibraryA(\"%s\") -> host DLL %s 0x%08X, refs %d\n", name, g_dll[i]->name,
                handle_of(i), g_refs[i]);
        RET(handle_of(i), 1);
    }
    if (w == TM_LOAD_FILE) {
        /* win32hle loads a full path from that path only: hand it the file
         * found (application or system directory), in a guest buffer. */
        static uint32_t buf;
        if (!buf) buf = hle_va(hle_alloc(1024));
        snprintf((char *)hle_ptr(buf), 1024, "%s", g);
        MEM32(g_esp + 4) = buf;
        delegate("LoadLibraryA");
        return;
    }
    if (w == TM_LOAD_NOT_FOUND) { hle_set_last_error(ERROR_MOD_NOT_FOUND); RET(0, 1); }
    delegate("LoadLibraryA");
}

static void k_GetModuleHandleA(void) {                  /* (name): no reference taken */
    const char *name = ASTR(0);
    if (name) {
        int i = host_dll_index(name);
        if (i >= 0 && (!has_path(name) || path_is_dll(name, i))) {
            if (g_refs[i] > 0) RET(handle_of(i), 1);
            hle_set_last_error(ERROR_MOD_NOT_FOUND);
            RET(0, 1);
        }
    }
    delegate("GetModuleHandleA");
}

static void k_GetProcAddress(void) {                    /* (module, name or ordinal) */
    uint32_t h = A32(0), p = A32(1);
    if (h >= HOST_DLL_BASE && h < HOST_DLL_BASE + HOST_DLL_STEP * TM_MAX_HOST_DLLS) {
        int i = handle_index(h);
        if (i < 0) { hle_set_last_error(ERROR_MOD_NOT_FOUND); RET(0, 2); }
        if (p >= 0x10000u) {
            const char *want = (const char *)hle_ptr(p);
            tm_export_state st = tm_loader_export(g_dll[i]->name, want);
            asked(i, want, st);
            if (st == TM_EXPORT_IMPLEMENTED)
                for (const tm_host_export *e = g_dll[i]->exports; e->name; e++)
                    if (!strcmp(e->name, want)) RET(hle_resolve(e->shim), 2);
            const char *why = "";
            for (const tm_host_export *e = g_dll[i]->exports; e->name; e++) if (!strcmp(e->name, want) && e->why) why = e->why;
            fprintf(stderr, "[loader] GetProcAddress(%s, \"%s\"): %s%s%s\n", g_dll[i]->name, want,
                    st == TM_EXPORT_NONE ? "not exported by this host DLL" : "export not implemented", *why ? ": " : "", why);
        } else {
            fprintf(stderr, "[loader] GetProcAddress(%s, #%u): ordinals are not exported by host DLLs\n", g_dll[i]->name, p);
        }
        hle_set_last_error(ERROR_PROC_NOT_FOUND);
        RET(0, 2);
    }
    if (h && !tm_loader_win32hle_module(h)) {          /* NULL: the main image, win32hle's rule */
        fprintf(stderr, "[loader] GetProcAddress(0x%08X): not a module handle\n", h);
        hle_set_last_error(ERROR_MOD_NOT_FOUND);
        RET(0, 2);
    }
    delegate("GetProcAddress");
}

static void k_FreeLibrary(void) {                       /* (module) */
    uint32_t h = A32(0);
    if (h >= HOST_DLL_BASE && h < HOST_DLL_BASE + HOST_DLL_STEP * TM_MAX_HOST_DLLS) {
        int i = handle_index(h);
        if (i < 0) { hle_set_last_error(ERROR_MOD_NOT_FOUND); RET(0, 1); }
        g_refs[i]--;
        if (!g_refs[i] && g_dll[i]->on_unload) g_dll[i]->on_unload();
        fprintf(stderr, "[loader] FreeLibrary(%s) -> refs %d%s\n", g_dll[i]->name, g_refs[i], g_refs[i] ? "" : ", unloaded");
        RET(1, 1);
    }
    if (!tm_loader_win32hle_module(h)) {               /* NULL included: no module is unloaded by it */
        fprintf(stderr, "[loader] FreeLibrary(0x%08X): not a module handle\n", h);
        hle_set_last_error(ERROR_MOD_NOT_FOUND);
        RET(0, 1);
    }
    delegate("FreeLibrary");
}

/* Truncation as Windows Vista and later: the result is cut to nSize - 1
 * characters and terminated, the return value is nSize, and the error is
 * ERROR_INSUFFICIENT_BUFFER. (Windows XP did not terminate it.) */
static void k_GetModuleFileNameA(void) {                /* (module, buf, size) */
    uint32_t h = A32(0), size = A32(2);
    char *buf = ASTR(1);
    if (h >= HOST_DLL_BASE && h < HOST_DLL_BASE + HOST_DLL_STEP * TM_MAX_HOST_DLLS) {
        int i = handle_index(h);
        if (i < 0) { hle_set_last_error(ERROR_MOD_NOT_FOUND); RET(0, 3); }
        char p[600];
        uint32_t n = (uint32_t)snprintf(p, sizeof p, "%s\\%s", tm_loader_dir(g_dll[i]), g_dll[i]->name);
        if (!buf || !size) { hle_set_last_error(ERROR_INSUFFICIENT_BUFFER); RET(0, 3); }
        if (n >= size) {
            memcpy(buf, p, size - 1), buf[size - 1] = 0;
            hle_set_last_error(ERROR_INSUFFICIENT_BUFFER);
            RET(size, 3);
        }
        memcpy(buf, p, n + 1);
        RET(n, 3);
    }
    delegate("GetModuleFileNameA");
}

const win32hle_shim tm_loader_overrides[] = {
    { "tm:LoadLibraryA",       k_LoadLibraryA },
    { "tm:GetModuleHandleA",   k_GetModuleHandleA },
    { "tm:GetProcAddress",     k_GetProcAddress },
    { "tm:FreeLibrary",        k_FreeLibrary },
    { "tm:GetModuleFileNameA", k_GetModuleFileNameA },
    { 0, 0 }
};
