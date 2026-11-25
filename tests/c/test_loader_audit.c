/*
 * Loader edge cases (src/host/tm_loader.c) and the guest-pointer rule: the
 * pseudo-module handle range is kept out of guest memory, stale and
 * malformed handles fail, reference counts cannot go below zero, and the
 * COM objects and vtables a host DLL hands out are guest addresses. No game
 * files.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "tm_loader.h"
#include "tm_dinput8.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}

static void k_A(void) { RET(0xAAAAu, 0); }
static void k_B(void) { RET(0xBBBBu, 0); }
static const win32hle_shim shims[] = { { "a.dll!F", k_A }, { "b.dll!F", k_B }, { 0, 0 } };
static const tm_host_export ea[] = { { "F", "a.dll!F", NULL }, { 0, 0, 0 } };
static const tm_host_export eb[] = { { "F", "b.dll!F", NULL }, { 0, 0, 0 } };
static const tm_host_dll da = { .name = "a.dll", .visible = 1, .loadable = 1, .exports = ea };
static const tm_host_dll db = { .name = "b.dll", .visible = 1, .loadable = 1, .exports = eb };

static char *g_str;
static uint32_t gs(const char *s) { strcpy(g_str, s); return hle_va(g_str); }
static uint32_t call(const char *name, int n, const uint32_t *args) { return hle_call_guest(hle_resolve(name), n, args); }
static uint32_t load(const char *n) { uint32_t a[1] = { gs(n) }; return call("tm:LoadLibraryA", 1, a); }
static uint32_t handle(const char *n) { uint32_t a[1] = { gs(n) }; return call("tm:GetModuleHandleA", 1, a); }
static uint32_t freelib(uint32_t h) { uint32_t a[1] = { h }; return call("tm:FreeLibrary", 1, a); }
static uint32_t proc(uint32_t h, uint32_t name_or_ordinal) { uint32_t a[2] = { h, name_or_ordinal }; return call("tm:GetProcAddress", 2, a); }
static uint32_t procn(uint32_t h, const char *n) {
    static char *pn; if (!pn) pn = hle_alloc(64);
    strcpy(pn, n);
    return proc(h, hle_va(pn));
}
static uint32_t err(void) { return hle_get_last_error(); }

/* Run f in a child: the signal it dies of (0 if it returns). */
static int dies_of(void (*f)(void)) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGSEGV, SIG_DFL), signal(SIGABRT, SIG_DFL), signal(SIGBUS, SIG_DFL);
        f();
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFSIGNALED(st) ? WTERMSIG(st) : 0;
}
static void read_handle_range(void) { volatile uint32_t v = MEM32(0x7D000000u); (void)v; }
static void read_system_range(void) { volatile uint32_t v = MEM32(0x7E000000u); (void)v; }
static void read_system_range_end(void) { volatile uint32_t v = MEM32(0x7EFFFFFCu); (void)v; }
static void read_unreserved(void) { volatile uint32_t v = MEM32(0x6F000000u); (void)v; }
static void host_pointer_to_guest(void) { int local = 0; volatile uint32_t v = hle_va(&local); (void)v; }

int main(void) {
    recomp_host_init();
    win32hle_register(shims);
    win32hle_register(tm_loader_overrides);
    tm_dinput8_init();                          /* first host DLL: the handle range is reserved here */
    check("host DLL a registered", tm_loader_add(&da));
    check("host DLL b registered", tm_loader_add(&db));
    mach_enter();
    g_str = hle_alloc(1024);

    /* ---- the handle range and guest memory ---- */
    check("the handle range cannot be reserved by anything else", hle_vm_reserve(0x7D000000u, 0x10000u) == 0);
    check("  nor its last block", hle_vm_reserve(0x7D0F0000u, 0x10000u) == 0);
    check("the block after it can", hle_vm_reserve(0x7D100000u, 0x10000u) == 0x7D100000u);
    check("the system-module range cannot be reserved", hle_vm_reserve(0x7E000000u, 0x10000u) == 0);
    check("  nor its last block", hle_vm_reserve(0x7EFF0000u, 0x10000u) == 0);
    check("  nor a block across its start", hle_vm_reserve(0x7DFF0000u, 0x20000u) == 0);
    tm_loader_reserve();                         /* a second call changes nothing */
    check("reading a system-module handle as memory faults (SIGSEGV)", dies_of(read_system_range) == SIGSEGV);
    check("  and at the end of the range", dies_of(read_system_range_end) == SIGSEGV);
    int below = 1;
    for (int k = 0; k < 64; k++) {                /* ordinary allocations never land in either range */
        uint32_t a = hle_vm_reserve(0, 0x100000u);
        below &= a && (a + 0x100000u <= 0x7D000000u || (a >= 0x7D100000u && a + 0x100000u <= 0x7E000000u));
    }
    check("64 MB of anywhere allocations stay out of both handle ranges", below);
    check("reading a handle as memory faults (SIGSEGV)", dies_of(read_handle_range) == SIGSEGV);
    check("so does a VA nobody reserved", dies_of(read_unreserved) == SIGSEGV);
    check("a host pointer handed to the guest stops the run (SIGABRT)", dies_of(host_pointer_to_guest) == SIGABRT);
    check("hle_va(NULL) is 0", hle_va(NULL) == 0);
    char *gp = hle_alloc(32);
    check("a guest allocation round-trips", gp && hle_ptr(hle_va(gp)) == gp && hle_va(gp) != 0);

    /* ---- stale handles: A unloaded, B loaded ---- */
    uint32_t ha = load("a.dll");
    check("A loads", ha >= 0x7D000000u && ha < 0x7D100000u);
    check("A's export runs", hle_call_guest(procn(ha, "F"), 0, NULL) == 0xAAAAu);
    check("A unloads", freelib(ha) == 1 && tm_loader_refs("a.dll") == 0);
    uint32_t hb = load("b.dll");
    check("B loads with a handle of its own", hb && hb != ha);
    hle_set_last_error(0);
    check("stale A: GetProcAddress fails, ERROR_MOD_NOT_FOUND", procn(ha, "F") == 0 && err() == 126);
    static uint32_t buf;
    if (!buf) buf = hle_va(hle_alloc(300));
    uint32_t mf[3] = { ha, buf, 300 };
    hle_set_last_error(0);
    check("stale A: GetModuleFileNameA fails", call("tm:GetModuleFileNameA", 3, mf) == 0 && err() == 126);
    hle_set_last_error(0);
    check("stale A: FreeLibrary fails, B untouched", freelib(ha) == 0 && err() == 126 && tm_loader_refs("b.dll") == 1);
    check("B's export is B's", hle_call_guest(procn(hb, "F"), 0, NULL) == 0xBBBBu);
    freelib(hb);

    /* ---- malformed handles in the range ---- */
    hle_set_last_error(0);
    check("a handle off the 64 KB step: ERROR_MOD_NOT_FOUND", procn(0x7D000004u, "F") == 0 && err() == 126);
    hle_set_last_error(0);
    check("a slot with no DLL registered: ERROR_MOD_NOT_FOUND", procn(0x7D0F0000u, "F") == 0 && err() == 126);
    hle_set_last_error(0);
    check("FreeLibrary of such a slot: FALSE", freelib(0x7D0F0000u) == 0 && err() == 126);

    /* ---- reference counts ---- */
    uint32_t h1 = load("a.dll"), h2 = load("A"), h3 = load("C:\\WINDOWS\\SYSTEM32\\A.DLL");
    check("three loads, one handle, refs 3", h1 && h1 == h2 && h2 == h3 && tm_loader_refs("a.dll") == 3);
    check("GetModuleHandleA takes no reference", handle("a.dll") == h1 && tm_loader_refs("a.dll") == 3);
    freelib(h1); freelib(h1);
    check("two FreeLibrary: refs 1, still loaded", tm_loader_refs("a.dll") == 1 && handle("a.dll") == h1);
    check("the third: unloaded", freelib(h1) == 1 && tm_loader_refs("a.dll") == 0);
    hle_set_last_error(0);
    check("a fourth: FALSE, refs stay 0 (no underflow)", freelib(h1) == 0 && err() == 126 && tm_loader_refs("a.dll") == 0);
    check("reload after unload: the same handle", load("a.dll") == h1 && tm_loader_refs("a.dll") == 1);

    /* ---- GetProcAddress ---- */
    hle_set_last_error(0);
    check("ordinal 1: NULL, ERROR_PROC_NOT_FOUND (host DLLs export by name only)", proc(h1, 1) == 0 && err() == 127);
    hle_set_last_error(0);
    check("a NULL name is ordinal 0: NULL, ERROR_PROC_NOT_FOUND", proc(h1, 0) == 0 && err() == 127);
    hle_set_last_error(0);
    check("ordinal 0xFFFF: NULL, ERROR_PROC_NOT_FOUND", proc(h1, 0xFFFFu) == 0 && err() == 127);
    check("names are case-sensitive", procn(h1, "f") == 0);
    uint32_t fa = procn(h1, "F");
    check("an export address is a shim VA: not guest memory, dispatched as an import",
          fa >= 0x7F000000u && fa < 0x80000000u && recomp_lookup_import(fa) != NULL);
    freelib(h1);

    /* ---- win32hle's modules: system pseudo-modules, the main image, bad handles ---- */
    hle_module_add("C:\\GAME\\T.EXE", 0x00400000u, 0, 0, 1);
    uint32_t k32 = handle("kernel32.dll");
    check("GetModuleHandleA(kernel32): a system pseudo-module in its range", k32 >= 0x7E000000u && k32 < 0x7F000000u);
    check("LoadLibraryA(kernel32): the same handle", load("KERNEL32") == k32);
    hle_set_last_error(1234);
    uint32_t gtc = procn(k32, "GetTickCount");
    check("GetProcAddress(kernel32, GetTickCount): an export, last error untouched", gtc && hle_get_last_error() == 1234);
    hle_set_last_error(0);
    check("GetProcAddress(kernel32, a name it lacks): ERROR_PROC_NOT_FOUND", procn(k32, "NoSuchExport") == 0 && err() == 127);
    check("FreeLibrary(kernel32): TRUE", freelib(k32) == 1);
    check("  still loaded afterwards (win32hle never unloads)", handle("kernel32.dll") == k32 && procn(k32, "GetTickCount") == gtc);
    check("FreeLibrary(the main image): TRUE", freelib(0x00400000u) == 1);
    hle_set_last_error(0);
    check("GetProcAddress(NULL, a name): the main image, ERROR_PROC_NOT_FOUND", procn(0, "Nothing") == 0 && err() == 127);
    static const uint32_t bad[] = { 0x12345678u, 0x7E000004u, 0x7EF00000u, 0x00400004u, 0xFFFFFFFFu };
    for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        char l[96];
        hle_set_last_error(0);
        snprintf(l, sizeof l, "GetProcAddress(0x%08X): NULL, ERROR_MOD_NOT_FOUND", bad[k]);
        check(l, procn(bad[k], "GetTickCount") == 0 && err() == 126);
        hle_set_last_error(0);
        snprintf(l, sizeof l, "FreeLibrary(0x%08X): FALSE, ERROR_MOD_NOT_FOUND", bad[k]);
        check(l, freelib(bad[k]) == 0 && err() == 126);
    }
    hle_set_last_error(0);
    check("FreeLibrary(NULL): FALSE, ERROR_MOD_NOT_FOUND", freelib(0) == 0 && err() == 126);
    check("tm_loader_win32hle_module: kernel32 yes, NULL and a host DLL handle no",
          tm_loader_win32hle_module(k32) && !tm_loader_win32hle_module(0) && !tm_loader_win32hle_module(0x7D000000u));
    uint32_t ha2 = load("a.dll");
    check("host DLLs unchanged: A loads, its export runs, it unloads",
          ha2 == ha && hle_call_guest(procn(ha2, "F"), 0, NULL) == 0xAAAAu && freelib(ha2) == 1 && tm_loader_refs("a.dll") == 0);
    hle_set_last_error(0);
    check("  and its stale handle still fails with ERROR_MOD_NOT_FOUND", freelib(ha2) == 0 && err() == 126 && procn(ha2, "F") == 0);

    /* ---- COM objects from a host DLL are guest memory ---- */
    uint32_t hd = load("dinput8.dll");
    uint32_t create = procn(hd, "DirectInput8Create");
    static uint32_t ppv, iid;
    if (!ppv) ppv = hle_va(hle_alloc(4)), iid = hle_va(hle_alloc(16));
    const uint8_t iid_di8a[16] = { 0x30,0x80,0x79,0xBF, 0x3A,0x48, 0xA2,0x4D, 0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00 };
    memcpy(hle_ptr(iid), iid_di8a, 16);
    uint32_t args[5] = { 0x00400000u, 0x0800u, iid, ppv, 0 };
    uint32_t hr = hle_call_guest(create, 5, args);
    uint32_t obj = MEM32(ppv), vt = obj ? MEM32(obj) : 0;
    check("DirectInput8Create: an object", hr == 0 && obj);
    check("  the object is below the shim range (guest memory)", obj && obj < 0x7F000000u);
    check("  its vtable is guest memory too", vt && vt < 0x7F000000u && vt != obj);
    int all = 1;
    for (int k = 0; k < 11 && vt; k++) all &= recomp_lookup_import(MEM32(vt + 4u * (uint32_t)k)) != NULL;
    check("  every one of its 11 vtable slots is a dispatchable shim", vt && all);
    uint32_t rel[1] = { obj };
    if (vt) hle_call_guest(MEM32(vt + 8), 1, rel);                 /* Release */
    freelib(hd);
    mach_leave();
    printf("loader audit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
