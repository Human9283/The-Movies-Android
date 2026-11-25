/*
 * d3d9.dll as a host DLL (src/host/tm_d3d9.c): loading it, Direct3DCreate9,
 * the IDirect3D9 methods that need no device, and the ones that must stop
 * the run because no device exists yet. No game files.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "tm_loader.h"
#include "tm_d3d9.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}

static const uint8_t IID_IUnknown[16]   = { 0,0,0,0, 0,0, 0,0, 0xC0,0,0,0,0,0,0,0x46 };
static const uint8_t IID_IDirect3D9[16] = { 0xCA,0xCB,0xBD,0x81, 0xD4,0x64, 0x6D,0x42, 0xAE,0x8D,0xAD,0x01,0x47,0xF4,0x27,0x5C };
static const uint8_t IID_IDirect3DDevice9[16] = { 0x96,0x3B,0x22,0xD0, 0x7A,0xBF, 0xFD,0x43, 0x92,0xBD,0xA4,0x3B,0x0D,0x82,0xB9,0xEB };

static uint32_t g_out, g_str;
static uint32_t api(const char *name, int n, const uint32_t *a) { return hle_call_guest(hle_resolve(name), n, a); }
static uint32_t gs(const char *s) { strcpy(hle_ptr(g_str), s); return g_str; }
static uint32_t vcall(uint32_t obj, int slot, int n, const uint32_t *args) {
    uint32_t a[8] = { obj };
    for (int i = 0; i < n; i++) a[1 + i] = args[i];
    return hle_call_guest(MEM32(MEM32(obj) + 4u * (uint32_t)slot), n + 1, a);
}
static uint32_t gcopy(const void *p, uint32_t n) { void *g = hle_alloc(n); memcpy(g, p, n); return hle_va(g); }

static uint32_t g_child;
static int stops(void (*f)(void)) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) { int dn = open("/dev/null", O_WRONLY); if (dn >= 0) dup2(dn, 2); f(); _exit(0); }
    int st = 0;
    waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st);
}
static uint32_t g_create;
static void create_wrong_sdk(void) { uint32_t a[1] = { 31 }; hle_call_guest(g_create, 1, a); }
static void get_caps(void) { uint32_t a[3] = { 0, 1, g_out }; vcall(g_child, 14, 3, a); }
static void create_device(void) { uint32_t a[6] = { 0, 1, 0, 0x40, g_out, g_out }; vcall(g_child, 16, 6, a); }
static void identifier(void) { uint32_t a[3] = { 0, 0, g_out }; vcall(g_child, 5, 3, a); }
static void check_format(void) { uint32_t a[6] = { 0, 1, 22, 0, 3, 21 }; vcall(g_child, 10, 6, a); }
static void after_release(void) { vcall(g_child, 1, 0, NULL); }

int main(void) {
    recomp_host_init();
    win32hle_register(tm_loader_overrides);
    tm_d3d9_init();
    mach_enter();
    g_out = hle_va(hle_alloc(1024));
    g_str = hle_va(hle_alloc(256));

    /* ---- the DLL ---- */
    uint32_t a1[1] = { gs("d3d9.dll") };
    uint32_t h = api("tm:LoadLibraryA", 1, a1);
    check("LoadLibraryA(d3d9.dll): a host DLL", h >= 0x7D000000u && h < 0x7D100000u);
    check("  listed as a host DLL of the system directory", tm_loader_visible("C:\\WINDOWS\\SYSTEM32", "D3D9.DLL") != NULL);
    uint32_t a2[2] = { h, gs("Direct3DCreate9") };
    g_create = api("tm:GetProcAddress", 2, a2);
    check("GetProcAddress(Direct3DCreate9): a shim VA", g_create >= 0x7F000000u && recomp_lookup_import(g_create) != NULL);
    uint32_t a3[2] = { h, gs("Direct3DCreate9Ex") };
    hle_set_last_error(0);
    check("Direct3DCreate9Ex: an export of the real DLL, not implemented: NULL, ERROR_PROC_NOT_FOUND",
          api("tm:GetProcAddress", 2, a3) == 0 && hle_get_last_error() == 127);

    /* ---- Direct3DCreate9 and IDirect3D9 ---- */
    uint32_t sdk[1] = { 32 };
    uint32_t d3d = hle_call_guest(g_create, 1, sdk);
    check("Direct3DCreate9(D3D_SDK_VERSION): an object in guest memory", d3d >= 0x10000u && d3d < 0x7D000000u);
    uint32_t vt = MEM32(d3d);
    int disp = vt >= 0x10000u && vt < 0x7D000000u;
    for (int k = 0; disp && k < 17; k++) disp = recomp_lookup_import(MEM32(vt + 4u * (uint32_t)k)) != NULL;
    check("  its vtable is guest memory, all 17 slots dispatchable", disp);
    check("  one object alive", tm_d3d9_live() == 1);

    uint32_t q[2] = { gcopy(IID_IDirect3D9, 16), g_out };
    check("QI(IDirect3D9): the same object", vcall(d3d, 0, 2, q) == 0 && MEM32(g_out) == d3d);
    uint32_t qu[2] = { gcopy(IID_IUnknown, 16), g_out };
    check("QI(IUnknown): the same object", vcall(d3d, 0, 2, qu) == 0 && MEM32(g_out) == d3d);
    uint32_t qd[2] = { gcopy(IID_IDirect3DDevice9, 16), g_out };
    check("QI(IDirect3DDevice9): E_NOINTERFACE, NULL", vcall(d3d, 0, 2, qd) == 0x80004002u && MEM32(g_out) == 0);
    uint32_t qn[2] = { q[0], 0 };
    check("QI with ppv NULL: E_POINTER", vcall(d3d, 0, 2, qn) == 0x80004003u);
    check("refs 3 after two QIs", vcall(d3d, 1, 0, NULL) == 4 && vcall(d3d, 2, 0, NULL) == 3);

    check("GetAdapterCount: 1", vcall(d3d, 4, 0, NULL) == 1);
    uint32_t m = g_out;
    memset(hle_ptr(m), 0xCC, 16);
    uint32_t gm[2] = { 0, m };
    uint32_t sw[1] = { 0 }, sh[1] = { 1 };
    uint32_t w = api("GetSystemMetrics", 1, sw), hh = api("GetSystemMetrics", 1, sh);
    check("GetAdapterDisplayMode(0): S_OK", vcall(d3d, 8, 2, gm) == 0);
    check("  the desktop GetSystemMetrics reports", MEM32(m) == w && MEM32(m + 4) == hh && w && hh);
    check("  refresh 0 (not known), D3DFMT_X8R8G8B8", MEM32(m + 8) == 0 && MEM32(m + 12) == 22);
    uint32_t gm1[2] = { 1, m }, gmn[2] = { 0, 0 };
    check("GetAdapterDisplayMode(1): D3DERR_INVALIDCALL", vcall(d3d, 8, 2, gm1) == 0x8876086Cu);
    check("GetAdapterDisplayMode(0, NULL): D3DERR_INVALIDCALL", vcall(d3d, 8, 2, gmn) == 0x8876086Cu);

    /* ---- display modes ---- */
    int ref[HLE_DD_MAX_MODES][2], dw = 0, dh = 0;
    hle_screen_desktop(&dw, &dh);
    int nref = hle_dd_modes(dw, dh, ref);
    uint32_t c22[2] = { 0, 22 }, c23[2] = { 0, 23 }, c24[2] = { 0, 24 }, c21[2] = { 0, 21 }, c25[2] = { 0, 25 },
             c20[2] = { 0, 20 }, c35[2] = { 0, 35 }, cbad[2] = { 1, 22 };
    check("GetAdapterModeCount(X8R8G8B8): DirectDraw's mode count", nref > 0 && vcall(d3d, 6, 2, c22) == (uint32_t)nref);
    check("  R5G6B5: the same", vcall(d3d, 6, 2, c23) == (uint32_t)nref);
    check("  X1R5G5B5: the same (565 and 555 are equivalent)", vcall(d3d, 6, 2, c24) == (uint32_t)nref);
    check("  A8R8G8B8, A1R5G5B5, R8G8B8, A2R10G10B10: 0 (not display formats here)",
          vcall(d3d, 6, 2, c21) == 0 && vcall(d3d, 6, 2, c25) == 0 && vcall(d3d, 6, 2, c20) == 0 && vcall(d3d, 6, 2, c35) == 0);
    check("  adapter 1: 0", vcall(d3d, 6, 2, cbad) == 0);
    int all = 1, sorted = 1;
    for (int k = 0; k < nref; k++) {
        uint32_t e[4] = { 0, 22, (uint32_t)k, m };
        all &= vcall(d3d, 7, 4, e) == 0 && MEM32(m) == (uint32_t)ref[k][0] && MEM32(m + 4) == (uint32_t)ref[k][1] &&
               MEM32(m + 8) == 0 && MEM32(m + 12) == 22;
        if (k) sorted &= ref[k][0] > ref[k - 1][0] || (ref[k][0] == ref[k - 1][0] && ref[k][1] > ref[k - 1][1]);
    }
    check("EnumAdapterModes(X8R8G8B8, each): DirectDraw's modes, refresh 0, the format asked", all);
    check("  smallest first", sorted);
    uint32_t e555[4] = { 0, 24, 0, m };
    check("EnumAdapterModes(X1R5G5B5, 0): the mode, Format R5G6B5", vcall(d3d, 7, 4, e555) == 0 && MEM32(m + 12) == 23);
    uint32_t eend[4] = { 0, 22, (uint32_t)nref, m }, ea8[4] = { 0, 21, 0, m }, enul[4] = { 0, 22, 0, 0 }, ead[4] = { 1, 22, 0, m };
    check("  past the last mode: D3DERR_INVALIDCALL", vcall(d3d, 7, 4, eend) == 0x8876086Cu);
    check("  a format with no modes: D3DERR_NOTAVAILABLE", vcall(d3d, 7, 4, ea8) == 0x8876086Au);
    check("  NULL: D3DERR_INVALIDCALL", vcall(d3d, 7, 4, enul) == 0x8876086Cu);
    check("  adapter 1: D3DERR_INVALIDCALL", vcall(d3d, 7, 4, ead) == 0x8876086Cu);

    /* ---- what needs a device stops the run ---- */
    g_child = d3d;
    check("Direct3DCreate9 with another SDK version stops the run", stops(create_wrong_sdk));
    check("GetDeviceCaps stops the run (no device to describe)", stops(get_caps));
    check("CreateDevice stops the run", stops(create_device));
    check("GetAdapterIdentifier stops the run", stops(identifier));
    check("CheckDeviceFormat stops the run", stops(check_format));

    check("Release x3: destroyed", vcall(d3d, 2, 0, NULL) == 2 && vcall(d3d, 2, 0, NULL) == 1 &&
          vcall(d3d, 2, 0, NULL) == 0 && tm_d3d9_live() == 0);
    check("a method after the last Release stops the run", stops(after_release));
    uint32_t fl[1] = { h };
    check("FreeLibrary(d3d9.dll): unloaded", api("tm:FreeLibrary", 1, fl) == 1 && tm_loader_refs("d3d9.dll") == 0);

    mach_leave();
    printf("d3d9: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
