/*
 * tm_host.c - the host for the lifted Movies.exe 1.1 (Russian CD) on
 * pcrecomp's win32hle: portable C, no Windows, no inline asm.
 *
 * M2 (proof of concept): run the lifted CRT from the PE entry point until it
 * calls WinMain, on a 64-bit host with the guest's 4 GB "based" in one
 * mapping. Success is printed as a `[poc] WinMain` line with the time and the
 * number of distinct lifted functions entered on the way.
 *
 *   tm_host <game dir> [--stop-at-winmain] [--watchdog S] [trace options]
 *
 * <game dir> holds Movies.exe and Data/ (tools/tm_install.py). It becomes the
 * guest's C:\ so the game sees itself as C:\Movies.exe.
 *
 * Built with -DRECOMP_TRACE: RECOMP_ENTER calls recomp_trace_enter, and our
 * recomp_trace_extra hook counts entries and watches for WinMain.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "pe_loader.h"
#include "recomp_trace.h"
#include "tm_shell.h"
#include "tm_user32.h"
#include "tm_kernel32.h"
#include "tm_except.h"
#include "tm_loader.h"
#include "tm_dinput8.h"
#include "tm_sysroot.h"
#include "tm_vfs.h"
#include "tm_wmvcore.h"
#include "tm_dshow.h"
#include "tm_d3d9.h"
#include "tm_ole32.h"
#include "tm_winmm.h"
#include "tm_d3d9_backend.h"
#ifdef TM_HAVE_DXVK
#include "tm_d3d9_dxvk.h"
#endif
#include "tm_imports.h"
#include "tm_itrace.h"

/* Movies.exe 1.1 (SHA-1 (hash removed)): CRT WinMainCRTStartup at 0x00A31571
 * calls WinMain at 0x00A316F0 (docs/AUDIT.md). */
#define TM_ENTRY_VA   0x00A31571u
#define TM_WINMAIN_VA 0x005303C0u

static int      g_stop_at_winmain;
static const char *g_itrace_path;   /* --import-trace FILE */
static const char *g_seen_path;     /* --seen-out FILE: the code VAs entered, one per line */
static void itrace_flush(void);
static uint64_t g_t0;
static uint32_t g_entries;          /* every RECOMP_ENTER */
static uint32_t g_distinct;         /* distinct VAs entered */
static uint8_t *g_seen;             /* one bit per 2 bytes of the image's code range */
#define SEEN_LO 0x00400000u
#define SEEN_HI 0x01100000u

static double ms_since_start(void) { return (double)(hle_mono_ns() - g_t0) / 1e6; }

static void on_enter(uint32_t va) {
    g_entries++;
    if (va >= SEEN_LO && va < SEEN_HI) {
        uint32_t bit = (va - SEEN_LO) >> 1;
        if (!(g_seen[bit >> 3] & (1u << (bit & 7)))) { g_seen[bit >> 3] |= (uint8_t)(1u << (bit & 7)); g_distinct++; }
    }
    if (va == TM_WINMAIN_VA) {
        fprintf(stderr, "[poc] WinMain 0x%08X entered: %.1f ms, %u function entries, %u distinct functions\n",
                va, ms_since_start(), g_entries, g_distinct);
        if (g_stop_at_winmain) {
            fprintf(stderr, "[poc] PASS: stopping at WinMain (--stop-at-winmain)\n");
            itrace_flush();
            fflush(stderr);
            _exit(0);
        }
    }
}

/* run_lift.py stubs every catalogued function it did not lift with this. */
void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "[not-lifted] sub_%08X after %u entries\n", va, g_entries);
    recomp_dump_trace("not lifted");
    itrace_flush();
    _exit(2);
}

/* ---- helper.dll: the Russian publisher's addition (docs/AUDIT.md 2.2) ----
 * ConvertSymbols(WCHAR *dst, const WCHAR *src, UINT n), stdcall: copy n
 * UTF-16 units, moving 0x00C0..0x00FF (CP1251 А..я read as Latin-1) up by
 * 0x350 to U+0410..U+044F. Does nothing if either pointer is NULL or n is 0. */
static void tm_ConvertSymbols(void) {
    uint32_t dst = A32(0), src = A32(1), n = A32(2);
    if (dst && src && n) {
        for (uint32_t i = 0; i < n; i++) {
            uint16_t c = MEM16(src + 2u * i);
            if (c >= 0xC0 && c <= 0xFF) c = (uint16_t)(c + 0x350);
            MEM16(dst + 2u * i) = c;
        }
    }
    RET(dst, 3);
}

static const win32hle_shim tm_helper_dll[] = {
    { "ConvertSymbols", tm_ConvertSymbols },
    { NULL, NULL }
};

/* The bind's resolver: a registered shim if there is one, else a self-naming
 * stub that stops the run when called. Every import gets a status
 * (tm_imports.h) and is recorded, so a permissive bind's "0 unresolved"
 * cannot hide an unsupported one. With --import-trace the IAT slot gets a
 * recording trampoline in front of the shim (tm_itrace.c). */
#define MAX_BOUND 1024
static struct { const char *name; tm_import_status st; } g_bound[MAX_BOUND];
static int g_nbound, g_status_count[TM_NSTATUS];
static int g_stub_count;
static uint32_t tm_resolve(const char *name) {
    char ov[128];
    snprintf(ov, sizeof ov, "tm:%s", name);              /* our override of a win32hle shim */
    uint32_t va = hle_resolve(ov);
    if (!va) va = hle_resolve(name);
    tm_import_status st;
    if (va) {
        int listed = tm_import_listed_status(name, NULL);
        st = listed >= 0 ? (tm_import_status)listed : TM_WIN32HLE_PRESENT;
    } else {
        st = TM_UNSUPPORTED;
        fprintf(stderr, "[bind] STUB %s\n", name);
        g_stub_count++;
        va = hle_resolve_or_stub(name);
    }
    if (g_nbound < MAX_BOUND) { g_bound[g_nbound].name = name; g_bound[g_nbound].st = st; g_nbound++; }
    g_status_count[st]++;
    return tm_itrace_wrap(name, va, (int)st);
}

static const char *status_name_int(int s) { return tm_import_status_name((tm_import_status)s); }
/* --seen-out: every VA in the image's code range a run entered (diagnostic;
 * the run itself is unchanged). */
static void seen_flush(void) {
    if (!g_seen_path || !g_seen) return;
    FILE *f = fopen(g_seen_path, "w");
    if (!f) return;
    for (uint32_t bit = 0; bit < (SEEN_HI - SEEN_LO) / 2; bit++)
        if (g_seen[bit >> 3] & (1u << (bit & 7))) fprintf(f, "%08X\n", SEEN_LO + 2u * bit);
    fclose(f);
}
static void itrace_flush(void) { if (g_itrace_path) tm_itrace_write(g_itrace_path, status_name_int); seen_flush(); }
static void on_hle_exit(int code) { (void)code; tm_dinput8_report(stderr); tm_dshow_report(stderr); tm_d3d9_report(stderr); tm_loader_report(stderr); itrace_flush(); }

static void write_imports_json(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[imports] cannot write %s\n", path); return; }
    fprintf(f, "{\n \"counts\": {");
    for (int s = 0; s < TM_NSTATUS; s++)
        fprintf(f, "%s\"%s\": %d", s ? ", " : "", tm_import_status_name((tm_import_status)s), g_status_count[s]);
    fprintf(f, "},\n \"imports\": [\n");
    for (int i = 0; i < g_nbound; i++) {
        const char *note = NULL;
        tm_import_listed_status(g_bound[i].name, &note);
        fprintf(f, "  {\"name\": \"%s\", \"status\": \"%s\"", g_bound[i].name, tm_import_status_name(g_bound[i].st));
        if (note) {
            fputs(", \"note\": \"", f);
            for (const char *p = note; *p; p++) { if (*p == '"' || *p == '\\') fputc('\\', f); fputc(*p, f); }
            fputc('"', f);
        }
        fprintf(f, "}%s\n", i + 1 < g_nbound ? "," : "");
    }
    fprintf(f, " ]\n}\n");
    fclose(f);
}

/* A crash report: what the lifted program was doing when the run stopped.
 * Registers are the guest's globals; with RECOMP_LOCAL_REGS a body keeps its
 * own copies and writes them back before every call, so these are the values
 * at the last call or return, which is where win32hle's aborts come from.
 * The guest stack's top dwords show the return addresses of the call chain. */
static void crash_report(const char *why, uintptr_t fault) {
    fprintf(stderr, "\n=== %s after %.1f ms ===\n", why, ms_since_start());
    fprintf(stderr, "  in lifted sub_%08X, last import %s\n", g_cur_func, g_cur_import ? g_cur_import : "?");
    if (fault) {
        uintptr_t lo = (uintptr_t)g_mem_base;
        if (fault >= lo && fault - lo < 0x100000000ull)
            fprintf(stderr, "  fault address %p = guest VA 0x%08X\n", (void *)fault, (uint32_t)(fault - lo));
        else
            fprintf(stderr, "  fault address %p (outside guest memory)\n", (void *)fault);
    }
    fprintf(stderr, "  eax=%08X ebx=%08X ecx=%08X edx=%08X\n  esi=%08X edi=%08X ebp=%08X esp=%08X\n",
            g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_ebp, g_esp);
    fprintf(stderr, "  %u function entries, %u distinct functions, WinMain %s\n", g_entries, g_distinct,
            g_seen && (g_seen[((TM_WINMAIN_VA - SEEN_LO) >> 1) >> 3] & (1u << (((TM_WINMAIN_VA - SEEN_LO) >> 1) & 7)))
                ? "ENTERED" : "not entered");
    if (g_esp >= 0x1000u && g_esp < 0xFFFFF000u) {
        fprintf(stderr, "  guest stack at esp:");
        for (int i = 0; i < 16; i++) {
            if (i % 4 == 0) fprintf(stderr, "\n   ");
            fprintf(stderr, " %08X", MEM32(g_esp + 4u * (uint32_t)i));
        }
        fprintf(stderr, "\n");
    }
    recomp_dump_trace(why);
    tm_dinput8_report(stderr);                          /* the same reports as a normal exit */
    tm_dshow_report(stderr);
    tm_d3d9_report(stderr);
    tm_loader_report(stderr);
    itrace_flush();
    fflush(stderr);
}

static void on_fatal_signal(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    const char *why = sig == SIGABRT ? "abort (win32hle fatal)" : sig == SIGSEGV ? "SIGSEGV"
                    : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : "signal";
    crash_report(why, (sig == SIGABRT) ? 0 : (uintptr_t)si->si_addr);
    _exit(3);
}

static void install_crash_handlers(void) {
    static char altstack[1 << 16];
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fatal_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    int sigs[] = { SIGABRT, SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    for (unsigned i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
}

static void on_alarm(int sig) {
    (void)sig;
    fprintf(stderr, "[watchdog] expired after %.0f ms: %u entries, %u distinct, in sub_%08X\n",
            ms_since_start(), g_entries, g_distinct, g_cur_func);
    recomp_dump_trace("watchdog");
    tm_dinput8_report(stderr), tm_dshow_report(stderr), tm_d3d9_report(stderr);   /* diagnostic, as the crash report */
    itrace_flush();
    _exit(4);
}

/* --d3d9 dxvk: Direct3D 9 devices on DXVK Native, shown in a win32hle window
 * made for Vulkan (docs/DXVK-INTEGRATION.md). Without it the run is
 * headless and the game's CreateDevice stops it. */
static int open_d3d9_backend(const char *name) {
    if (strcmp(name, "dxvk")) { fprintf(stderr, "[host] --d3d9 %s: the only backend is dxvk\n", name); return 0; }
#ifdef TM_HAVE_DXVK
    hle_screen_gl = 2;
    if (hle_screen_open("The Movies", 0, 0) != 0) { fprintf(stderr, "[host] --d3d9 dxvk: no window\n"); return 0; }
    tm_d3d9_set_backend(tm_d3d9_dxvk_backend());
    fprintf(stderr, "[host] Direct3D 9 devices: DXVK Native\n");
    return 1;
#else
    fprintf(stderr, "[host] --d3d9 dxvk: built without DXVK (configure with -DTM_DXVK_PREFIX)\n");
    return 0;
#endif
}

int main(int argc, char **argv) {
    const char *game = NULL, *user = "tm_user", *system_dir = "tm_system", *imports_json = NULL;
    int watchdog = 0;
    const char *d3d9_backend = NULL;
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--stop-at-winmain")) g_stop_at_winmain = 1;
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) watchdog = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--user") && i + 1 < argc) user = argv[++i];
        else if (!strcmp(argv[i], "--system") && i + 1 < argc) system_dir = argv[++i];
        else if (!strcmp(argv[i], "--imports-json") && i + 1 < argc) imports_json = argv[++i];
        else if (!strcmp(argv[i], "--import-trace") && i + 1 < argc) g_itrace_path = argv[++i];
        else if (!strcmp(argv[i], "--seen-out") && i + 1 < argc) g_seen_path = argv[++i];
        else if (!strcmp(argv[i], "--d3d9") && i + 1 < argc) d3d9_backend = argv[++i];
        else if (!strcmp(argv[i], "--mouse") && i + 1 < argc && !strcmp(argv[i + 1], "free")) hle_screen_confine(0), i++;
        else if (argv[i][0] != '-' && !game) game = argv[i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 64; }
    }
    if (!game) {
        fprintf(stderr, "usage: %s <game dir> [--stop-at-winmain] [--watchdog S] [--user DIR] [--system DIR] [--imports-json FILE] [--import-trace FILE] [--seen-out FILE] [--d3d9 dxvk]\n", argv[0]);
        recomp_trace_help();
        return 64;
    }
    g_seen = calloc((SEEN_HI - SEEN_LO) / 16 + 1, 1);
    recomp_trace_extra = on_enter;
    install_crash_handlers();
    if (g_itrace_path) {
        if (!tm_itrace_selfcheck()) { fprintf(stderr, "[itrace] trampoline table is wrong\n"); return 70; }
        tm_itrace_enable();
    }
    g_t0 = hle_mono_ns();
    if (watchdog > 0) { signal(SIGALRM, on_alarm); alarm((unsigned)watchdog); }

    char exe[4096];
    snprintf(exe, sizeof exe, "%s/Movies.exe", game);
    hle_set_drive('c', game);

    /* host.c's boot, with our resolver (recomp_host_boot_permissive takes none) */
    recomp_host_init();
    tm_loader_reserve();                                /* module handle ranges, before guest allocations */
    win32hle_register(tm_helper_dll);
    win32hle_register(tm_shell_shims);
    win32hle_register(tm_user32_shims);
    win32hle_register(tm_kernel32_shims);
    win32hle_register(tm_except_overrides);
    win32hle_register(tm_loader_overrides);
    win32hle_register(tm_kernel32_overrides);
    win32hle_register(tm_sysroot_overrides);
    win32hle_register(tm_vfs_overrides);
    win32hle_register(tm_ole32_overrides);
    win32hle_register(tm_winmm_overrides);
    tm_dinput8_init();
    tm_wmvcore_init();
    tm_dshow_init();
    tm_d3d9_init();
    if (d3d9_backend && !open_d3d9_backend(d3d9_backend)) return 1;
    hle_on_exit = on_hle_exit;                          /* ExitProcess, fatal stops: keep the trace */
    tm_shell_init(user);
    if (!tm_sysroot_init(game, system_dir, user)) return 1;
    pe_image img;
    if (recomp_pe_map(exe, &img) != 0) { fprintf(stderr, "[host] cannot map %s\n", exe); return 1; }
    char guest[1024];
    hle_guest_path(exe, guest, sizeof guest);
    hle_module_add(guest, img.base, img.resource_rva, img.resource_size, 1);
    tm_loader_set_app_dir(guest);
    int unresolved = recomp_pe_bind(&img, tm_resolve);
    fprintf(stderr, "[bind] %d imports are stubs (stop the run if called), %d unresolved\n",
            g_stub_count, unresolved);
    fprintf(stderr, "[imports] %d bound: %d implemented, %d diagnostic, %d win32hle-present, %d win32hle-verified, %d unsupported\n",
            g_nbound, g_status_count[TM_IMPLEMENTED], g_status_count[TM_DIAGNOSTIC], g_status_count[TM_WIN32HLE_PRESENT],
            g_status_count[TM_WIN32HLE_VERIFIED], g_status_count[TM_UNSUPPORTED]);
    if (imports_json) write_imports_json(imports_json);
    if (img.entry != TM_ENTRY_VA)
        fprintf(stderr, "[host] warning: entry 0x%08X, expected 0x%08X (not the 1.1 CD exe?)\n",
                img.entry, TM_ENTRY_VA);
    fprintf(stderr, "[host] guest %s, boot %.1f ms, entering lifted 0x%08X (%u functions in dispatch)\n",
            guest, ms_since_start(), img.entry, recomp_dispatch_count);
    uint32_t args[1];
    uint32_t rc = hle_call_guest(img.entry, 0, args);
    fprintf(stderr, "[host] entry returned %u after %.1f ms, %u entries, %u distinct\n",
            rc, ms_since_start(), g_entries, g_distinct);
    itrace_flush();
    return (int)rc;
}
