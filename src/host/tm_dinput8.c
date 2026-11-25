/*
 * tm_dinput8.c - dinput8.dll as far as Movies.exe uses it (docs/IMPORTS.md,
 * "DirectInput 8").
 *
 * What the game calls, from its own code (sub_009F9950, the input start-up):
 *   DirectInput8Create(GetModuleHandleA(NULL), 0x0800, IID_IDirectInput8A, &di, NULL)
 *   di->CreateDevice(GUID_SysKeyboard, &kbd, NULL)    then the same for GUID_SysMouse
 *   dev->SetDataFormat(c_dfDIKeyboard / c_dfDIMouse)  (the tables linked into the exe)
 *   dev->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND)
 *   dev->SetProperty(DIPROP_BUFFERSIZE, { DIPH_DEVICE, 128 })
 *   dev->Acquire()
 * It checks only the results of DirectInput8Create and CreateDevice.
 *
 * Implemented, each with the checks and HRESULTs of the DirectInput 8
 * documentation and, where that is silent, of Wine's dinput (noted):
 *   DirectInput8Create; IDirectInput8A QueryInterface, AddRef, Release,
 *   CreateDevice (system keyboard and mouse only); IDirectInputDevice8A
 *   QueryInterface, AddRef, Release, SetDataFormat, SetCooperativeLevel,
 *   SetProperty (DIPROP_BUFFERSIZE only), Acquire, Unacquire,
 *   GetDeviceState, GetDeviceData (the standard keyboard and mouse formats).
 * Every other method is a self-naming stub that stops the run when called:
 * it is implemented when the game is seen to call it, not before.
 *
 * Input: win32hle's keyboard and mouse state, which the host feeds from SDL
 * in a windowed run (see "input" below). A headless run has no source, so
 * its devices report no change.
 *
 * COM: objects live in guest memory; [obj] is the vtable, an array of shim
 * VAs; every method is stdcall with `this` first. Host-side state is kept
 * per object, found by its guest address.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "win32hle.h"
#include "tm_loader.h"
#include "tm_dinput8.h"

#define DI_OK                     0x00000000u
#define DI_NOEFFECT               0x00000001u   /* S_FALSE */
#define E_NOINTERFACE             0x80004002u
#define E_POINTER                 0x80004003u
#define DIERR_UNSUPPORTED         0x80004001u   /* E_NOTIMPL */
#define DIERR_NOAGGREGATION       0x80040110u   /* CLASS_E_NOAGGREGATION */
#define DIERR_DEVICENOTREG        0x80040154u   /* REGDB_E_CLASSNOTREG */
#define DIERR_OTHERAPPHASPRIO     0x80070005u   /* E_ACCESSDENIED */
#define E_HANDLE                  0x80070006u
#define DIERR_INVALIDPARAM        0x80070057u   /* E_INVALIDARG */
#define DIERR_ACQUIRED            0x800700AAu
#define DIERR_OLDDIRECTINPUTVERSION  0x8007047Eu
#define DIERR_BETADIRECTINPUTVERSION 0x80070481u
#define DIERR_NOTACQUIRED         0x8007000Cu
#define DIERR_NOTBUFFERED         0x80040207u
#define DI_BUFFEROVERFLOW         0x00000001u
#define DIGDD_PEEK                0x1u
enum { FMT_KEYBOARD = 1, FMT_MOUSE = 2 };

#define DISCL_EXCLUSIVE    0x1u
#define DISCL_NONEXCLUSIVE 0x2u
#define DISCL_FOREGROUND   0x4u
#define DISCL_BACKGROUND   0x8u
#define DISCL_NOWINKEY     0x10u
#define WS_CHILD           0x40000000u

typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } guid_t;
static const guid_t IID_IUnknown_          = {0x00000000, 0x0000, 0x0000, {0xC0,0,0,0,0,0,0,0x46}};
static const guid_t IID_IDirectInput8A_    = {0xBF798030, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const guid_t IID_IDirectInput8W_    = {0xBF798031, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const guid_t IID_IDirectInputDevice8A_ = {0x54D41080, 0xDC15, 0x4833, {0xA4,0x1B,0x74,0x8F,0x73,0xA3,0x81,0x79}};
static const guid_t GUID_SysMouse_         = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0,0}};
static const guid_t GUID_SysKeyboard_      = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0,0}};

static int guid_at(uint32_t va, const guid_t *g) { return va && !memcmp(hle_ptr(va), g, 16); }
static void guid_str(uint32_t va, char *out, size_t n) {
    if (!va) { snprintf(out, n, "NULL"); return; }
    const uint8_t *b = hle_ptr(va);
    guid_t g; memcpy(&g, b, 16);
    snprintf(out, n, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", g.d1, g.d2, g.d3,
             g.d4[0], g.d4[1], g.d4[2], g.d4[3], g.d4[4], g.d4[5], g.d4[6], g.d4[7]);
}

/* ---- the call log: every call of the first 16 per method, a count of all ---- */
typedef struct { const char *name; uint64_t calls; } method_count;
static method_count g_counts[64];
static int g_ncounts;
static int log_call(const char *method) {
    for (int i = 0; i < g_ncounts; i++)
        if (g_counts[i].name == method) return ++g_counts[i].calls <= 16;
    if (g_ncounts < 64) { g_counts[g_ncounts].name = method; g_counts[g_ncounts++].calls = 1; }
    return 1;
}
#define LOG(method, ...) do { if (log_call(method)) { \
        fprintf(stderr, "[dinput8] thread %ld %s", (long)syscall(SYS_gettid), method); \
        fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

void tm_dinput8_report(FILE *f) {
    for (int i = 0; i < g_ncounts; i++)
        fprintf(f, "[dinput8] %s: %llu call%s\n", g_counts[i].name, (unsigned long long)g_counts[i].calls,
                g_counts[i].calls == 1 ? "" : "s");
}

/* ---- objects ---- */
enum { OBJ_DI = 1, OBJ_DEV = 2 };
enum { DEV_KEYBOARD = 1, DEV_MOUSE = 2 };
typedef struct {
    uint32_t va;                 /* the guest object: [va] = vtable */
    int      kind, refs;
    /* devices */
    int      dev, has_format, acquired;
    uint32_t parent;             /* the IDirectInput8A that created it (holds a reference) */
    uint32_t coop, hwnd;         /* coop 0: never set (non-exclusive, background) */
    uint32_t data_size, nobjs, buffer_size;
    /* input, while acquired (GetDeviceState, GetDeviceData) */
    int      fmt;                /* the data format: FMT_KEYBOARD (c_dfDIKeyboard), FMT_MOUSE (c_dfDIMouse), or 0 */
    int      last_x, last_y, last_z;                    /* GetDeviceState's: where its last read left the axes */
    int      buf_x, buf_y, buf_z;                       /* GetDeviceData's: what the events so far describe */
    uint8_t  buf_buttons[3], buf_keys[256];
    struct { uint32_t ofs, data, time, seq; } q[256];
    int      qn, overflow;
} obj_t;
#define MAX_OBJS 64
static obj_t g_obj[MAX_OBJS];
static uint32_t g_vt_di, g_vt_dev;

static obj_t *obj_of(uint32_t va, int kind) {
    for (int i = 0; i < MAX_OBJS; i++) if (g_obj[i].va && g_obj[i].va == va && g_obj[i].kind == kind) return &g_obj[i];
    return NULL;
}
static obj_t *obj_new(int kind, uint32_t vtable) {
    for (int i = 0; i < MAX_OBJS; i++)
        if (!g_obj[i].va) {
            uint8_t *p = hle_alloc(16);
            if (!p) return NULL;
            memset(&g_obj[i], 0, sizeof g_obj[i]);
            g_obj[i].va = hle_va(p);
            g_obj[i].kind = kind;
            g_obj[i].refs = 1;
            MEM32(g_obj[i].va) = vtable;
            return &g_obj[i];
        }
    return NULL;
}
static const char *dev_name(const obj_t *o) { return o->dev == DEV_KEYBOARD ? "keyboard" : "mouse"; }

/* ---- IDirectInput8A ---- */
static uint32_t di_release(obj_t *o);
static void baseline(obj_t *o);
static void relative_mouse(void);

static void di_QueryInterface(void) {                   /* (this, riid, ppv) */
    obj_t *o = obj_of(A32(0), OBJ_DI);
    uint32_t riid = A32(1), ppv = A32(2);
    char g[40]; guid_str(riid, g, sizeof g);
    if (!ppv) { LOG("IDirectInput8A::QueryInterface", "(%s, NULL) -> E_POINTER", g); RET(E_POINTER, 3); }
    if (o && (guid_at(riid, &IID_IUnknown_) || guid_at(riid, &IID_IDirectInput8A_))) {
        o->refs++;
        MEM32(ppv) = o->va;
        LOG("IDirectInput8A::QueryInterface", "(%s) -> S_OK, refs %d", g, o->refs);
        RET(DI_OK, 3);
    }
    MEM32(ppv) = 0;
    LOG("IDirectInput8A::QueryInterface", "(%s) -> E_NOINTERFACE", g);
    RET(E_NOINTERFACE, 3);
}
static void di_AddRef(void) {
    obj_t *o = obj_of(A32(0), OBJ_DI);
    if (!o) { LOG("IDirectInput8A::AddRef", "(unknown object 0x%08X)", A32(0)); RET(0, 1); }
    o->refs++;
    LOG("IDirectInput8A::AddRef", "() -> %d", o->refs);
    RET((uint32_t)o->refs, 1);
}
static uint32_t di_release(obj_t *o) {
    if (--o->refs > 0) return (uint32_t)o->refs;
    o->va = 0;
    return 0;
}
static void di_Release(void) {
    obj_t *o = obj_of(A32(0), OBJ_DI);
    if (!o) { LOG("IDirectInput8A::Release", "(unknown object 0x%08X)", A32(0)); RET(0, 1); }
    uint32_t r = di_release(o);
    LOG("IDirectInput8A::Release", "() -> %u", r);
    RET(r, 1);
}
static void di_CreateDevice(void) {                     /* (this, rguid, lplpDevice, pUnkOuter) */
    obj_t *o = obj_of(A32(0), OBJ_DI);
    uint32_t rguid = A32(1), out = A32(2), outer = A32(3);
    char g[40]; guid_str(rguid, g, sizeof g);
    if (!o) { LOG("IDirectInput8A::CreateDevice", "(unknown object 0x%08X)", A32(0)); RET(DIERR_INVALIDPARAM, 4); }
    if (!out) { LOG("IDirectInput8A::CreateDevice", "(%s, NULL) -> E_POINTER", g); RET(E_POINTER, 4); }
    MEM32(out) = 0;
    if (!rguid) { LOG("IDirectInput8A::CreateDevice", "(NULL) -> E_POINTER"); RET(E_POINTER, 4); }
    if (outer) { LOG("IDirectInput8A::CreateDevice", "(%s, outer 0x%08X) -> DIERR_NOAGGREGATION", g, outer); RET(DIERR_NOAGGREGATION, 4); }
    int dev = guid_at(rguid, &GUID_SysKeyboard_) ? DEV_KEYBOARD : guid_at(rguid, &GUID_SysMouse_) ? DEV_MOUSE : 0;
    if (!dev) { LOG("IDirectInput8A::CreateDevice", "(%s) -> DIERR_DEVICENOTREG (only the system keyboard and mouse)", g); RET(DIERR_DEVICENOTREG, 4); }
    obj_t *d = obj_new(OBJ_DEV, g_vt_dev);
    if (!d) RET(0x8007000Eu, 4);                         /* E_OUTOFMEMORY */
    d->dev = dev;
    d->parent = o->va;
    o->refs++;                                          /* the device holds its DirectInput object (Wine) */
    MEM32(out) = d->va;
    LOG("IDirectInput8A::CreateDevice", "(%s = GUID_Sys%s) -> S_OK, device 0x%08X", g, dev == DEV_KEYBOARD ? "Keyboard" : "Mouse", d->va);
    RET(DI_OK, 4);
}

/* ---- IDirectInputDevice8A ---- */
static void dev_QueryInterface(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t riid = A32(1), ppv = A32(2);
    char g[40]; guid_str(riid, g, sizeof g);
    if (!ppv) { LOG("IDirectInputDevice8A::QueryInterface", "(%s, NULL) -> E_POINTER", g); RET(E_POINTER, 3); }
    if (o && (guid_at(riid, &IID_IUnknown_) || guid_at(riid, &IID_IDirectInputDevice8A_))) {
        o->refs++;
        MEM32(ppv) = o->va;
        LOG("IDirectInputDevice8A::QueryInterface", "(%s) -> S_OK, refs %d", g, o->refs);
        RET(DI_OK, 3);
    }
    MEM32(ppv) = 0;
    LOG("IDirectInputDevice8A::QueryInterface", "(%s) -> E_NOINTERFACE", g);
    RET(E_NOINTERFACE, 3);
}
static void dev_AddRef(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    if (!o) RET(0, 1);
    o->refs++;
    LOG("IDirectInputDevice8A::AddRef", "(%s) -> %d", dev_name(o), o->refs);
    RET((uint32_t)o->refs, 1);
}
static void dev_Release(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    if (!o) { LOG("IDirectInputDevice8A::Release", "(unknown object 0x%08X)", A32(0)); RET(0, 1); }
    int r = --o->refs;
    LOG("IDirectInputDevice8A::Release", "(%s) -> %d%s", dev_name(o), r, r ? "" : ", destroyed");
    if (!r) {
        o->acquired = 0;
        obj_t *p = obj_of(o->parent, OBJ_DI);
        o->va = 0;
        if (p) di_release(p);
    }
    RET((uint32_t)r, 1);
}
static void dev_SetDataFormat(void) {                   /* (this, lpdf) */
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t df = A32(1);
    if (!o) RET(DIERR_INVALIDPARAM, 2);
    if (o->acquired) { LOG("IDirectInputDevice8A::SetDataFormat", "(%s) -> DIERR_ACQUIRED", dev_name(o)); RET(DIERR_ACQUIRED, 2); }
    if (!df) { LOG("IDirectInputDevice8A::SetDataFormat", "(%s, NULL) -> E_POINTER", dev_name(o)); RET(E_POINTER, 2); }
    uint32_t size = MEM32(df), objsize = MEM32(df + 4), flags = MEM32(df + 8), data = MEM32(df + 12), n = MEM32(df + 16),
             rg = MEM32(df + 20);
    int ok = size == 24 && objsize == 16 && data && data % 4 == 0 && (!n || rg);
    for (uint32_t k = 0; ok && k < n; k++) if (MEM32(rg + 16 * k + 4) >= data) ok = 0;   /* each object inside the data */
    if (!ok) {
        LOG("IDirectInputDevice8A::SetDataFormat", "(%s, size %u, objsize %u, data %u, %u objects) -> DIERR_INVALIDPARAM",
            dev_name(o), size, objsize, data, n);
        RET(DIERR_INVALIDPARAM, 2);
    }
    o->has_format = 1, o->data_size = data, o->nobjs = n;
    /* the standard formats: c_dfDIKeyboard (256 bytes, 256 objects, offset = DIK code), c_dfDIMouse
     * (16 bytes, 7 objects: X 0, Y 4, Z 8, buttons 12..15) */
    o->fmt = o->dev == DEV_KEYBOARD && data == 256 && n == 256 ? FMT_KEYBOARD
           : o->dev == DEV_MOUSE && data == 16 && n == 7 && MEM32(rg + 4) == 0 && MEM32(rg + 16 + 4) == 4 &&
             MEM32(rg + 32 + 4) == 8 ? FMT_MOUSE : 0;
    LOG("IDirectInputDevice8A::SetDataFormat", "(%s, flags 0x%X, data %u bytes, %u objects) -> DI_OK", dev_name(o), flags, data, n);
    RET(DI_OK, 2);
}
static void dev_SetCooperativeLevel(void) {             /* (this, hwnd, flags) */
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t hwnd = A32(1), fl = A32(2), hr;
    if (!o) RET(DIERR_INVALIDPARAM, 3);
    uint32_t ex = fl & (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE), fg = fl & (DISCL_FOREGROUND | DISCL_BACKGROUND);
    if (o->acquired) hr = DIERR_ACQUIRED;
    else if (fl & ~(DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE | DISCL_FOREGROUND | DISCL_BACKGROUND | DISCL_NOWINKEY) ||
             (ex != DISCL_EXCLUSIVE && ex != DISCL_NONEXCLUSIVE) || (fg != DISCL_FOREGROUND && fg != DISCL_BACKGROUND))
        hr = DIERR_INVALIDPARAM;                        /* exactly one of each pair */
    else if (!hwnd && fl == (DISCL_NONEXCLUSIVE | DISCL_BACKGROUND)) hr = DI_OK;   /* NULL: the desktop (Wine) */
    else if (!hle_is_window(hwnd) || (hle_window_style(hwnd) & WS_CHILD)) hr = E_HANDLE;  /* a top-level window */
    else if (ex == DISCL_EXCLUSIVE && fg == DISCL_BACKGROUND) hr = DIERR_UNSUPPORTED;      /* not for keyboard/mouse */
    else hr = DI_OK;
    if (hr == DI_OK) o->coop = fl, o->hwnd = hwnd;
    LOG("IDirectInputDevice8A::SetCooperativeLevel", "(%s, hwnd 0x%08X%s, flags 0x%X) -> 0x%08X", dev_name(o), hwnd,
        hwnd && !hle_is_window(hwnd) ? " (not a window)" : !hwnd ? " (NULL)" : "", fl, hr);
    RET(hr, 3);
}
static void dev_SetProperty(void) {                     /* (this, rguidProp, pdiph) */
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t prop = A32(1), ph = A32(2);
    if (!o) RET(DIERR_INVALIDPARAM, 3);
    if (prop != 1u) {                                   /* DIPROP_BUFFERSIZE = MAKEDIPROP(1) */
        fprintf(stderr, "[dinput8] IDirectInputDevice8A::SetProperty(%s, property 0x%08X): not implemented\n", dev_name(o), prop);
        hle_exit(3);
    }
    if (!ph) { LOG("IDirectInputDevice8A::SetProperty", "(%s, DIPROP_BUFFERSIZE, NULL) -> DIERR_INVALIDPARAM", dev_name(o)); RET(DIERR_INVALIDPARAM, 3); }
    uint32_t size = MEM32(ph), hsize = MEM32(ph + 4), objn = MEM32(ph + 8), how = MEM32(ph + 12), val = MEM32(ph + 16), hr;
    if (size != 20 || hsize != 16 || how != 0 || objn != 0) hr = DIERR_INVALIDPARAM;   /* a DIPROPDWORD, DIPH_DEVICE */
    else if (o->acquired) hr = DIERR_ACQUIRED;
    else { o->buffer_size = val; hr = DI_OK; }
    LOG("IDirectInputDevice8A::SetProperty", "(%s, DIPROP_BUFFERSIZE, size %u, header %u, obj %u, how %u, %u) -> 0x%08X",
        dev_name(o), size, hsize, objn, how, val, hr);
    RET(hr, 3);
}
static void dev_Acquire(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t hr;
    if (!o) RET(DIERR_INVALIDPARAM, 1);
    if (!o->has_format) hr = DIERR_INVALIDPARAM;
    else if (o->acquired) hr = DI_NOEFFECT;
    else if ((o->coop & DISCL_FOREGROUND)) {
        uint32_t fgw = hle_call_guest(hle_resolve("GetForegroundWindow"), 0, NULL);
        hr = fgw == o->hwnd ? DI_OK : DIERR_OTHERAPPHASPRIO;
    } else hr = DI_OK;
    if (hr == DI_OK) o->acquired = 1, baseline(o), relative_mouse();
    LOG("IDirectInputDevice8A::Acquire", "(%s, coop 0x%X) -> 0x%08X", dev_name(o), o->coop, hr);
    RET(hr, 1);
}
static void dev_Unacquire(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    if (!o) RET(DIERR_INVALIDPARAM, 1);
    uint32_t hr = o->acquired ? DI_OK : DI_NOEFFECT;
    o->acquired = 0;
    relative_mouse();
    LOG("IDirectInputDevice8A::Unacquire", "(%s) -> 0x%08X", dev_name(o), hr);
    RET(hr, 1);
}

/* ---- input: win32hle's keyboard and mouse state (user32.c), which the host
 * feeds from SDL; the same state a game's window messages come from ----
 * The keyboard reports DIK scan codes, the mouse relative axes (the movement,
 * edges or not: hle_input_raw) and three buttons. Buffered data: each change
 * is an event, recorded when input arrives (hle_input_hook) and at each read,
 * so a press and release between two reads are both kept. An exclusive mouse
 * asks the host for movement alone (hle_input_relative_hook), as Windows
 * gives it. The virtual key to DIK table is win32hle's (dinput.c, MIT). */
static uint8_t g_dik[256];
static void dik_table(void) {
    if (g_dik[0x1B]) return;
    static const struct { uint8_t vk, dik; } t[] = {
        { 0x1B, 0x01 }, { 0x31, 0x02 }, { 0x32, 0x03 }, { 0x33, 0x04 }, { 0x34, 0x05 }, { 0x35, 0x06 }, { 0x36, 0x07 },
        { 0x37, 0x08 }, { 0x38, 0x09 }, { 0x39, 0x0A }, { 0x30, 0x0B }, { 0xBD, 0x0C }, { 0xBB, 0x0D }, { 0x08, 0x0E },
        { 0x09, 0x0F }, { 'Q', 0x10 }, { 'W', 0x11 }, { 'E', 0x12 }, { 'R', 0x13 }, { 'T', 0x14 }, { 'Y', 0x15 },
        { 'U', 0x16 }, { 'I', 0x17 }, { 'O', 0x18 }, { 'P', 0x19 }, { 0xDB, 0x1A }, { 0xDD, 0x1B }, { 0x0D, 0x1C },
        { 0xA2, 0x1D }, { 'A', 0x1E }, { 'S', 0x1F }, { 'D', 0x20 }, { 'F', 0x21 }, { 'G', 0x22 }, { 'H', 0x23 },
        { 'J', 0x24 }, { 'K', 0x25 }, { 'L', 0x26 }, { 0xBA, 0x27 }, { 0xDE, 0x28 }, { 0xC0, 0x29 }, { 0xA0, 0x2A },
        { 0xDC, 0x2B }, { 'Z', 0x2C }, { 'X', 0x2D }, { 'C', 0x2E }, { 'V', 0x2F }, { 'B', 0x30 }, { 'N', 0x31 },
        { 'M', 0x32 }, { 0xBC, 0x33 }, { 0xBE, 0x34 }, { 0xBF, 0x35 }, { 0xA1, 0x36 }, { 0x6A, 0x37 }, { 0xA4, 0x38 },
        { 0x20, 0x39 }, { 0x14, 0x3A }, { 0x70, 0x3B }, { 0x71, 0x3C }, { 0x72, 0x3D }, { 0x73, 0x3E }, { 0x74, 0x3F },
        { 0x75, 0x40 }, { 0x76, 0x41 }, { 0x77, 0x42 }, { 0x78, 0x43 }, { 0x79, 0x44 }, { 0x90, 0x45 }, { 0x91, 0x46 },
        { 0x67, 0x47 }, { 0x68, 0x48 }, { 0x69, 0x49 }, { 0x6D, 0x4A }, { 0x64, 0x4B }, { 0x65, 0x4C }, { 0x66, 0x4D },
        { 0x6B, 0x4E }, { 0x61, 0x4F }, { 0x62, 0x50 }, { 0x63, 0x51 }, { 0x60, 0x52 }, { 0x6E, 0x53 }, { 0x7A, 0x57 },
        { 0x7B, 0x58 }, { 0xA3, 0x9D }, { 0x6F, 0xB5 }, { 0xA5, 0xB8 }, { 0x13, 0xC5 }, { 0x24, 0xC7 }, { 0x26, 0xC8 },
        { 0x21, 0xC9 }, { 0x25, 0xCB }, { 0x27, 0xCD }, { 0x23, 0xCF }, { 0x28, 0xD0 }, { 0x22, 0xD1 }, { 0x2D, 0xD2 },
        { 0x2E, 0xD3 }, { 0x5B, 0xDB }, { 0x5C, 0xDC }, { 0x5D, 0xDD },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) g_dik[t[i].vk] = t[i].dik;
}
/* the keyboard as DIK bytes (0x80 down); generic Shift, Control, Alt count as the left ones */
static void keyboard(uint8_t out[256]) {
    dik_table();
    memset(out, 0, 256);
    for (int vk = 1; vk < 256; vk++)
        if ((hle_input_vk((uint32_t)vk) & 0x80) && g_dik[vk]) out[g_dik[vk]] = 0x80;
    if (hle_input_vk(0x10) & 0x80 && !out[0x36]) out[0x2A] = 0x80;
    if (hle_input_vk(0x11) & 0x80 && !out[0x9D]) out[0x1D] = 0x80;
    if (hle_input_vk(0x12) & 0x80 && !out[0xB8]) out[0x38] = 0x80;
}
static void buttons(uint8_t out[3]) {
    out[0] = hle_input_vk(1) & 0x80, out[1] = hle_input_vk(2) & 0x80, out[2] = hle_input_vk(4) & 0x80;
}
static uint32_t g_seq;
static void push(obj_t *o, uint32_t ofs, uint32_t data) {
    static int trace = -1;                              /* TM_DINPUT_TRACE=1: every buffered event, for input debugging */
    if (trace < 0) trace = getenv("TM_DINPUT_TRACE") != NULL;
    if (trace) fprintf(stderr, "[dinput8] event %s ofs %u data %d at %u ms\n", dev_name(o), ofs, (int)data, hle_ticks_ms());
    uint32_t cap = o->buffer_size < 256 ? o->buffer_size : 256;
    if ((uint32_t)o->qn >= cap) { o->overflow = 1; return; }
    o->q[o->qn].ofs = ofs, o->q[o->qn].data = data, o->q[o->qn].time = hle_ticks_ms(), o->q[o->qn].seq = ++g_seq;
    o->qn++;
}
static void gather(obj_t *o) {
    if (o->dev == DEV_MOUSE) {
        int x, y;
        uint8_t b[3];
        hle_input_raw(&x, &y);
        if (x != o->buf_x) push(o, 0, (uint32_t)(x - o->buf_x));
        if (y != o->buf_y) push(o, 4, (uint32_t)(y - o->buf_y));
        o->buf_x = x, o->buf_y = y;
        int z = hle_input_wheel_total();
        if (z != o->buf_z) push(o, 8, (uint32_t)(z - o->buf_z)), o->buf_z = z;
        buttons(b);
        for (int i = 0; i < 3; i++)
            if (b[i] != o->buf_buttons[i]) push(o, 12u + (uint32_t)i, b[i]), o->buf_buttons[i] = b[i];
    } else {
        uint8_t k[256];
        keyboard(k);
        for (int i = 0; i < 256; i++)
            if (k[i] != o->buf_keys[i]) push(o, (uint32_t)i, k[i]), o->buf_keys[i] = k[i];
    }
}
static void input_arrived(void) {
    for (int i = 0; i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_DEV && g_obj[i].acquired) gather(&g_obj[i]);
}
/* at Acquire: reads start from the state now */
static void baseline(obj_t *o) {
    hle_input_raw(&o->last_x, &o->last_y);
    o->buf_x = o->last_x, o->buf_y = o->last_y;
    o->last_z = o->buf_z = hle_input_wheel_total();
    buttons(o->buf_buttons);
    keyboard(o->buf_keys);
    o->qn = 0, o->overflow = 0;
}
static void relative_mouse(void) {
    int on = 0;
    for (int i = 0; i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_DEV && g_obj[i].dev == DEV_MOUSE && g_obj[i].acquired &&
            (g_obj[i].coop & DISCL_EXCLUSIVE)) on = 1;
    static int was = -1;
    if (on != was && hle_input_relative_hook) hle_input_relative_hook(on);
    was = on;
}

static void dev_GetDeviceState(void) {                  /* (this, cbData, lpvData) */
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t n = A32(1), p = A32(2), hr;
    if (!o) RET(DIERR_INVALIDPARAM, 3);
    if (!o->fmt) hle_fatal("[dinput8] GetDeviceState(%s): the data format is not c_dfDIKeyboard / c_dfDIMouse (not handled)", dev_name(o));
    if (!p || n != o->data_size) hr = DIERR_INVALIDPARAM;
    else if (!o->acquired) hr = DIERR_NOTACQUIRED;
    else {
        memset(hle_ptr(p), 0, n);
        if (o->dev == DEV_MOUSE) {                      /* DIMOUSESTATE: lX, lY, lZ, rgbButtons[4] */
            int x, y, z = hle_input_wheel_total();
            uint8_t b[3];
            hle_input_raw(&x, &y);
            buttons(b);
            MEM32(p) = (uint32_t)(x - o->last_x), MEM32(p + 4) = (uint32_t)(y - o->last_y), MEM32(p + 8) = (uint32_t)(z - o->last_z);
            o->last_x = x, o->last_y = y, o->last_z = z;
            for (uint32_t i = 0; i < 3; i++) MEM8(p + 12 + i) = b[i];
        } else {
            uint8_t k[256];
            keyboard(k);
            memcpy(hle_ptr(p), k, 256);
        }
        hr = DI_OK;
    }
    LOG("IDirectInputDevice8A::GetDeviceState", "(%s, %u bytes) -> 0x%08X", dev_name(o), n, hr);
    RET(hr, 3);
}
/* (this, cbObjectData, rgdod, pdwInOut, dwFlags): DIDEVICEOBJECTDATA in
 * DirectInput 8 is 20 bytes (ofs, data, time stamp, sequence, uAppData). */
static void dev_GetDeviceData(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    uint32_t sz = A32(1), out = A32(2), io = A32(3), flags = A32(4), hr;
    if (!o) RET(DIERR_INVALIDPARAM, 5);
    if (!o->fmt) hle_fatal("[dinput8] GetDeviceData(%s): the data format is not c_dfDIKeyboard / c_dfDIMouse (not handled)", dev_name(o));
    if (!io || (sz != 20 && sz != 16) || (flags & ~DIGDD_PEEK)) { hr = DIERR_INVALIDPARAM; goto done; }
    if (!o->buffer_size) { hr = DIERR_NOTBUFFERED; goto done; }
    if (!o->acquired) { hr = DIERR_NOTACQUIRED; goto done; }
    gather(o);
    hr = o->overflow ? DI_BUFFEROVERFLOW : DI_OK;
    uint32_t want = MEM32(io), n = (uint32_t)o->qn < want ? (uint32_t)o->qn : want;
    for (uint32_t i = 0; out && i < n; i++) {
        uint32_t e = out + i * sz;
        MEM32(e) = o->q[i].ofs, MEM32(e + 4) = o->q[i].data, MEM32(e + 8) = o->q[i].time, MEM32(e + 12) = o->q[i].seq;
        if (sz == 20) MEM32(e + 16) = 0;                /* uAppData: no action map */
    }
    if (!(flags & DIGDD_PEEK)) {
        memmove(o->q, o->q + n, sizeof o->q[0] * (size_t)(o->qn - (int)n));
        o->qn -= (int)n, o->overflow = 0;
    }
    MEM32(io) = n;
    if (n) LOG("IDirectInputDevice8A::GetDeviceData", "(%s, %u wanted) -> 0x%08X, %u events", dev_name(o), want, hr, n);
    RET(hr, 5);
done:
    LOG("IDirectInputDevice8A::GetDeviceData", "(%s, size %u, flags 0x%X) -> 0x%08X", dev_name(o), sz, flags, hr);
    RET(hr, 5);
}

/* ---- DirectInput8Create ---- */
static void k_DirectInput8Create(void) {               /* (hinst, version, riid, ppvOut, punkOuter) */
    uint32_t hinst = A32(0), ver = A32(1), riid = A32(2), out = A32(3), outer = A32(4), hr;
    char g[40]; guid_str(riid, g, sizeof g);
    if (!out) { LOG("DirectInput8Create", "(0x%08X, 0x%X, %s, NULL) -> E_POINTER", hinst, ver, g); RET(E_POINTER, 5); }
    MEM32(out) = 0;
    /* Order of checks as Wine's dinput: interface, aggregation, then Initialize (instance, version). */
    if (guid_at(riid, &IID_IDirectInput8W_)) hr = E_NOINTERFACE;      /* the W interface is not implemented */
    else if (!guid_at(riid, &IID_IDirectInput8A_) && !guid_at(riid, &IID_IUnknown_)) hr = E_NOINTERFACE;
    else if (outer) hr = DIERR_NOAGGREGATION;
    else if (!hinst) hr = DIERR_INVALIDPARAM;
    else if (ver < 0x0800) hr = DIERR_OLDDIRECTINPUTVERSION;
    else if (ver > 0x0800) hr = DIERR_BETADIRECTINPUTVERSION;
    else {
        obj_t *o = obj_new(OBJ_DI, g_vt_di);
        if (!o) RET(0x8007000Eu, 5);
        MEM32(out) = o->va;
        LOG("DirectInput8Create", "(hinst 0x%08X, 0x%X, %s) -> DI_OK, object 0x%08X", hinst, ver, g, o->va);
        RET(DI_OK, 5);
    }
    LOG("DirectInput8Create", "(hinst 0x%08X, 0x%X, %s, outer 0x%08X) -> 0x%08X", hinst, ver, g, outer, hr);
    RET(hr, 5);
}

/* ---- vtables ---- */
static const char *const di_methods[11] = {
    "QueryInterface", "AddRef", "Release", "CreateDevice", "EnumDevices", "GetDeviceStatus", "RunControlPanel",
    "Initialize", "FindDevice", "EnumDevicesBySemantics", "ConfigureDevices" };
static const char *const dev_methods[32] = {
    "QueryInterface", "AddRef", "Release", "GetCapabilities", "EnumObjects", "GetProperty", "SetProperty",
    "Acquire", "Unacquire", "GetDeviceState", "GetDeviceData", "SetDataFormat", "SetEventNotification",
    "SetCooperativeLevel", "GetObjectInfo", "GetDeviceInfo", "RunControlPanel", "Initialize", "CreateEffect",
    "EnumEffects", "GetEffectInfo", "GetForceFeedbackState", "SendForceFeedbackCommand",
    "EnumCreatedEffectObjects", "Escape", "Poll", "SendDeviceData", "EnumEffectsInFile", "WriteEffectToFile",
    "BuildActionMap", "SetActionMap", "GetImageInfo" };

static const win32hle_shim g_impl[] = {
    { "dinput8.dll!DirectInput8Create",              k_DirectInput8Create },
    { "IDirectInput8A::QueryInterface",              di_QueryInterface },
    { "IDirectInput8A::AddRef",                      di_AddRef },
    { "IDirectInput8A::Release",                     di_Release },
    { "IDirectInput8A::CreateDevice",                di_CreateDevice },
    { "IDirectInputDevice8A::QueryInterface",        dev_QueryInterface },
    { "IDirectInputDevice8A::AddRef",                dev_AddRef },
    { "IDirectInputDevice8A::Release",               dev_Release },
    { "IDirectInputDevice8A::SetProperty",           dev_SetProperty },
    { "IDirectInputDevice8A::Acquire",               dev_Acquire },
    { "IDirectInputDevice8A::Unacquire",             dev_Unacquire },
    { "IDirectInputDevice8A::SetDataFormat",         dev_SetDataFormat },
    { "IDirectInputDevice8A::SetCooperativeLevel",   dev_SetCooperativeLevel },
    { "IDirectInputDevice8A::GetDeviceState",        dev_GetDeviceState },
    { "IDirectInputDevice8A::GetDeviceData",         dev_GetDeviceData },
    { 0, 0 }
};

/* A vtable in guest memory: an implemented method's shim, or a stub that
 * names itself and stops the run. */
static uint32_t make_vtable(const char *iface, const char *const *methods, int n) {
    uint32_t vt = hle_va(hle_alloc(4u * (uint32_t)n));
    for (int k = 0; k < n; k++) {
        char nm[96];
        snprintf(nm, sizeof nm, "%s::%s", iface, methods[k]);
        uint32_t va = hle_resolve(nm);
        if (!va) {
            static char names[64][96];
            static int nn;
            char *keep = nn < 64 ? names[nn++] : NULL;
            if (keep) snprintf(keep, 96, "%s", nm);
            va = hle_resolve_or_stub(keep ? keep : "dinput8 method");
        }
        MEM32(vt + 4u * (uint32_t)k) = va;
    }
    return vt;
}

static const tm_host_export g_exports[] = {
    { "DirectInput8Create", "dinput8.dll!DirectInput8Create", NULL },
    { 0, 0, 0 }
};
/* In the system directory, listed there and loadable; no attach or detach
 * work (the objects are made by DirectInput8Create). */
static const tm_host_dll g_dll = {
    .name = "dinput8.dll", .dir = TM_SYSTEM_DIR, .visible = 1, .loadable = 1, .exports = g_exports,
};

void tm_dinput8_init(void) {
    win32hle_register(g_impl);
    hle_input_hook = input_arrived;
    g_vt_di = make_vtable("IDirectInput8A", di_methods, 11);
    g_vt_dev = make_vtable("IDirectInputDevice8A", dev_methods, 32);
    tm_loader_add(&g_dll);
}
