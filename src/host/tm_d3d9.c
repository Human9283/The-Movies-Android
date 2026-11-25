/*
 * tm_d3d9.c - d3d9.dll as a host DLL: the start of the recording Direct3D 9
 * layer (docs/M3.md 3.4, docs/D3D9-REQUIREMENTS.md).
 *
 * What the game does first, from its own code (sub_0093D780, the graphics
 * start-up, called with the main window, which it stores at 0x00FA7128):
 *   d3d = Direct3DCreate9(D3D_SDK_VERSION = 32)        (stored at 0x00FA7CB8)
 *   d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode at 0x00FA7CC0)
 *
 * Implemented, and only what can be answered truthfully without a
 * rendering backend:
 *   Direct3DCreate9(32): an IDirect3D9 object;
 *   IDirect3D9 QueryInterface, AddRef, Release;
 *   GetAdapterCount: 1 (the guest has one display);
 *   GetAdapterDisplayMode: the guest's desktop as the rest of the host
 *     reports it (GetSystemMetrics' size, 32 bits per pixel as GetDeviceCaps
 *     says: D3DFMT_X8R8G8B8), refresh rate 0 (not known; the D3D9
 *     documentation lets 0 stand for the adapter default);
 *   GetAdapterModeCount, EnumAdapterModes: the display modes DirectDraw
 *     offers here too (win32hle's hle_dd_modes: its table, with the host
 *     display's size in its place), smallest first, refresh 0, in
 *     D3DFMT_X8R8G8B8 and D3DFMT_R5G6B5. D3DFMT_X1R5G5B5 is answered as
 *     R5G6B5: the documentation says 565 and 555 are treated as equivalent
 *     and the mode's Format says which the display uses. Any other format,
 *     the alpha ones and A2R10G10B10 included, has no display modes here
 *     (count 0, D3DERR_NOTAVAILABLE).
 *   CreateDevice: diagnostic only. It records the call (adapter, device
 *     type, focus window, behaviour flags and every D3DPRESENT_PARAMETERS
 *     field) and then stops the run: there is no device to create.
 * Every other method is a self-naming stub that stops the run: adapter
 * identification, mode lists, format and device checks, caps and
 * CreateDevice all describe a device, and there is none yet. They are
 * answered once a backend exists and can say what it does.
 *
 * COM: the object is guest memory whose first dword is its vtable (shim VAs
 * in guest memory); host state is found by the guest address. The vtable is
 * made at the first Direct3DCreate9, so the guest heap before it is laid out
 * as without this file.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "tm_loader.h"
#include "tm_d3d9.h"

#define S_OK                 0x00000000u
#define E_NOINTERFACE        0x80004002u
#define E_POINTER            0x80004003u
#define D3DERR_INVALIDCALL   0x8876086Cu
#define D3DERR_NOTAVAILABLE  0x8876086Au
#define D3DFMT_R5G6B5        23u
#define D3DFMT_X1R5G5B5      24u
#define D3D_SDK_VERSION      32u
#define D3DFMT_X8R8G8B8      22u
#define SM_CXSCREEN          0u
#define SM_CYSCREEN          1u

static const uint8_t IID_IUnknown_[16]   = { 0,0,0,0, 0,0, 0,0, 0xC0,0,0,0,0,0,0,0x46 };
static const uint8_t IID_IDirect3D9_[16] = { 0xCA,0xCB,0xBD,0x81, 0xD4,0x64, 0x6D,0x42, 0xAE,0x8D,0xAD,0x01,0x47,0xF4,0x27,0x5C };

#define MAX_OBJS 8
static struct { uint32_t va; int refs; } g_obj[MAX_OBJS];
static uint32_t g_vt;
static int g_created, g_destroyed;

static int obj_index(uint32_t va) {
    for (int i = 0; i < MAX_OBJS; i++) if (va && g_obj[i].va == va) return i;
    return -1;
}
static int self(const char *method) {
    int i = obj_index(A32(0));
    if (i < 0) hle_fatal("[d3d9] IDirect3D9::%s on 0x%08X: not a live IDirect3D9", method, A32(0));
    return i;
}

/* The guest's desktop, as GetSystemMetrics answers it. */
static uint32_t metric(uint32_t index) {
    uint32_t a[1] = { index };
    return hle_call_guest(hle_resolve("GetSystemMetrics"), 1, a);
}

static void d3d_QueryInterface(void) {                  /* (this, riid, ppv) */
    int i = self("QueryInterface");
    uint32_t riid = A32(1), ppv = A32(2);
    if (!ppv) RET(E_POINTER, 3);
    MEM32(ppv) = 0;
    if (!riid) RET(E_POINTER, 3);
    const uint8_t *g = hle_ptr(riid);
    if (!memcmp(g, IID_IUnknown_, 16) || !memcmp(g, IID_IDirect3D9_, 16)) {
        g_obj[i].refs++;
        MEM32(ppv) = g_obj[i].va;
        RET(S_OK, 3);
    }
    char s[40];
    hle_guid_string(g, s);
    fprintf(stderr, "[d3d9] IDirect3D9::QueryInterface(%s) -> E_NOINTERFACE\n", s);
    RET(E_NOINTERFACE, 3);
}
static void d3d_AddRef(void) { int i = self("AddRef"); RET((uint32_t)++g_obj[i].refs, 1); }
static void d3d_Release(void) {
    int i = self("Release");
    int n = --g_obj[i].refs;
    if (!n) {
        fprintf(stderr, "[d3d9] IDirect3D9 0x%08X destroyed\n", g_obj[i].va);
        hle_free(hle_ptr(g_obj[i].va));
        g_obj[i].va = 0;
        g_destroyed++;
    }
    RET((uint32_t)n, 1);
}
static void d3d_GetAdapterCount(void) {
    self("GetAdapterCount");
    fprintf(stderr, "[d3d9] IDirect3D9::GetAdapterCount() -> 1\n");
    RET(1, 1);
}
static void d3d_GetAdapterDisplayMode(void) {           /* (this, adapter, D3DDISPLAYMODE *) */
    self("GetAdapterDisplayMode");
    uint32_t adapter = A32(1), mode = A32(2);
    if (adapter != 0 || !mode) {
        fprintf(stderr, "[d3d9] IDirect3D9::GetAdapterDisplayMode(%u, 0x%08X) -> D3DERR_INVALIDCALL\n", adapter, mode);
        RET(D3DERR_INVALIDCALL, 3);
    }
    uint32_t w = metric(SM_CXSCREEN), h = metric(SM_CYSCREEN);
    MEM32(mode) = w, MEM32(mode + 4) = h, MEM32(mode + 8) = 0, MEM32(mode + 12) = D3DFMT_X8R8G8B8;
    fprintf(stderr, "[d3d9] IDirect3D9::GetAdapterDisplayMode(0) -> S_OK, %ux%u, refresh 0, D3DFMT_X8R8G8B8\n", w, h);
    RET(S_OK, 3);
}

/* The display's modes in a format: 0 if it has none in that format. */
static int modes_of(uint32_t fmt, int (*m)[2], uint32_t *as) {
    if (fmt == D3DFMT_X8R8G8B8) *as = D3DFMT_X8R8G8B8;
    else if (fmt == D3DFMT_R5G6B5 || fmt == D3DFMT_X1R5G5B5) *as = D3DFMT_R5G6B5;
    else return 0;
    int dw = 0, dh = 0;
    hle_screen_desktop(&dw, &dh);                       /* 0 headless: the table alone */
    return hle_dd_modes(dw, dh, m);
}
static void d3d_GetAdapterModeCount(void) {             /* (this, adapter, format) */
    self("GetAdapterModeCount");
    uint32_t adapter = A32(1), fmt = A32(2), as;
    int m[HLE_DD_MAX_MODES][2];
    uint32_t n = adapter == 0 ? (uint32_t)modes_of(fmt, m, &as) : 0;
    fprintf(stderr, "[d3d9] IDirect3D9::GetAdapterModeCount(%u, format %u) -> %u\n", adapter, fmt, n);
    RET(n, 3);
}
static void d3d_EnumAdapterModes(void) {                /* (this, adapter, format, mode, D3DDISPLAYMODE *) */
    self("EnumAdapterModes");
    uint32_t adapter = A32(1), fmt = A32(2), k = A32(3), out = A32(4), as = 0;
    int m[HLE_DD_MAX_MODES][2];
    if (adapter != 0 || !out) RET(D3DERR_INVALIDCALL, 5);
    int n = modes_of(fmt, m, &as);
    if (!n) RET(D3DERR_NOTAVAILABLE, 5);
    if (k >= (uint32_t)n) RET(D3DERR_INVALIDCALL, 5);
    MEM32(out) = (uint32_t)m[k][0], MEM32(out + 4) = (uint32_t)m[k][1], MEM32(out + 8) = 0, MEM32(out + 12) = as;
    RET(S_OK, 5);
}

/* CreateDevice: what the game asks for, then the stop (no device backend). */
static const char *fmt_name(uint32_t f) {
    switch (f) {
        case 0: return "UNKNOWN"; case 20: return "R8G8B8"; case 21: return "A8R8G8B8"; case 22: return "X8R8G8B8";
        case 23: return "R5G6B5"; case 24: return "X1R5G5B5"; case 25: return "A1R5G5B5"; case 26: return "A4R4G4B4";
        case 70: return "D16_LOCKABLE"; case 71: return "D32"; case 73: return "D15S1"; case 75: return "D24S8";
        case 77: return "D24X8"; case 79: return "D24X4S4"; case 80: return "D16";
        default: return "?";
    }
}
static void d3d_CreateDevice(void) {   /* (this, adapter, type, focus, flags, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **) */
    self("CreateDevice");
    uint32_t adapter = A32(1), type = A32(2), focus = A32(3), flags = A32(4), pp = A32(5), out = A32(6);
    fprintf(stderr, "[d3d9] IDirect3D9::CreateDevice(adapter %u, type %u%s, focus 0x%08X, flags 0x%08X, pp 0x%08X, out 0x%08X)\n",
            adapter, type, type == 1 ? " (HAL)" : type == 2 ? " (REF)" : type == 3 ? " (SW)" : "", focus, flags, pp, out);
    if (pp) {
        static const char *const f[14] = { "BackBufferWidth", "BackBufferHeight", "BackBufferFormat", "BackBufferCount",
            "MultiSampleType", "MultiSampleQuality", "SwapEffect", "hDeviceWindow", "Windowed", "EnableAutoDepthStencil",
            "AutoDepthStencilFormat", "Flags", "FullScreen_RefreshRateInHz", "PresentationInterval" };
        for (int k = 0; k < 14; k++) {
            uint32_t v = MEM32(pp + 4u * (uint32_t)k);
            fprintf(stderr, "[d3d9]   %-26s 0x%08X (%u)%s%s\n", f[k], v, v,
                    k == 2 || k == 10 ? " D3DFMT_" : "", k == 2 || k == 10 ? fmt_name(v) : "");
        }
    }
    hle_fatal("[d3d9] IDirect3D9::CreateDevice: no Direct3D 9 device backend exists yet (docs/D3D9-REQUIREMENTS.md)");
}

static const char *const d3d_methods[] = {
    "QueryInterface", "AddRef", "Release", "RegisterSoftwareDevice", "GetAdapterCount", "GetAdapterIdentifier",
    "GetAdapterModeCount", "EnumAdapterModes", "GetAdapterDisplayMode", "CheckDeviceType", "CheckDeviceFormat",
    "CheckDeviceMultiSampleType", "CheckDepthStencilMatch", "CheckDeviceFormatConversion", "GetDeviceCaps",
    "GetAdapterMonitor", "CreateDevice", NULL };
static const win32hle_shim g_impl[] = {
    { "IDirect3D9::QueryInterface",        d3d_QueryInterface },
    { "IDirect3D9::AddRef",                d3d_AddRef },
    { "IDirect3D9::Release",               d3d_Release },
    { "IDirect3D9::GetAdapterCount",       d3d_GetAdapterCount },
    { "IDirect3D9::GetAdapterDisplayMode", d3d_GetAdapterDisplayMode },
    { "IDirect3D9::GetAdapterModeCount",   d3d_GetAdapterModeCount },
    { "IDirect3D9::EnumAdapterModes",      d3d_EnumAdapterModes },
    { "IDirect3D9::CreateDevice",          d3d_CreateDevice },
    { 0, 0 }
};

static uint32_t make_vtable(void) {
    int n = 0;
    while (d3d_methods[n]) n++;
    uint32_t vt = hle_va(hle_alloc(4u * (uint32_t)n));
    static char names[32][48];
    for (int k = 0; k < n; k++) {
        snprintf(names[k], sizeof names[k], "IDirect3D9::%s", d3d_methods[k]);
        uint32_t va = hle_resolve(names[k]);
        MEM32(vt + 4u * (uint32_t)k) = va ? va : hle_resolve_or_stub(names[k]);   /* a stub stops the run, naming it */
    }
    return vt;
}

static void k_Direct3DCreate9(void) {                   /* (SDKVersion) */
    uint32_t sdk = A32(0);
    if (sdk != D3D_SDK_VERSION)
        hle_fatal("[d3d9] Direct3DCreate9(%u): only D3D_SDK_VERSION %u is implemented", sdk, D3D_SDK_VERSION);
    if (!g_vt) g_vt = make_vtable();
    for (int i = 0; i < MAX_OBJS; i++)
        if (!g_obj[i].va) {
            uint32_t *o = hle_alloc(16);
            if (!o) break;
            o[0] = g_vt;
            g_obj[i].va = hle_va(o), g_obj[i].refs = 1;
            g_created++;
            fprintf(stderr, "[d3d9] Direct3DCreate9(%u) -> IDirect3D9 0x%08X\n", sdk, g_obj[i].va);
            RET(g_obj[i].va, 1);
        }
    fprintf(stderr, "[d3d9] Direct3DCreate9(%u) -> NULL (out of objects)\n", sdk);
    RET(0, 1);
}

static const win32hle_shim g_exports_impl[] = {
    { "d3d9.dll!Direct3DCreate9", k_Direct3DCreate9 },
    { 0, 0 }
};
static const tm_host_export g_exports[] = {
    { "Direct3DCreate9", "d3d9.dll!Direct3DCreate9", NULL },
    { "Direct3DCreate9Ex", NULL, "Direct3D 9Ex (Vista and later) is not implemented; the game does not import it" },
    { 0, 0, 0 }
};
static const tm_host_dll g_dll = {
    .name = "d3d9.dll", .dir = TM_SYSTEM_DIR, .visible = 1, .loadable = 1, .exports = g_exports,
};

void tm_d3d9_init(void) {
    win32hle_register(g_exports_impl);
    win32hle_register(g_impl);
    tm_loader_add(&g_dll);
}

int tm_d3d9_live(void) {
    int n = 0;
    for (int i = 0; i < MAX_OBJS; i++) n += g_obj[i].va != 0;
    return n;
}

void tm_d3d9_report(FILE *f) {
    if (g_created) fprintf(f, "[d3d9] IDirect3D9: %d created, %d destroyed\n", g_created, g_destroyed);
}
