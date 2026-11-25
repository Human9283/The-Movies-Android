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
 *   SetProperty (DIPROP_BUFFERSIZE only), Acquire, Unacquire.
 * Every other method is a self-naming stub that stops the run when called:
 * it is implemented when the game is seen to call it, not before.
 *
 * No input reaches these devices yet: the host has no keyboard or mouse
 * source. Acquiring a device is real state, not working input.
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
    if (hr == DI_OK) o->acquired = 1;
    LOG("IDirectInputDevice8A::Acquire", "(%s, coop 0x%X) -> 0x%08X", dev_name(o), o->coop, hr);
    RET(hr, 1);
}
static void dev_Unacquire(void) {
    obj_t *o = obj_of(A32(0), OBJ_DEV);
    if (!o) RET(DIERR_INVALIDPARAM, 1);
    uint32_t hr = o->acquired ? DI_OK : DI_NOEFFECT;
    o->acquired = 0;
    LOG("IDirectInputDevice8A::Unacquire", "(%s) -> 0x%08X", dev_name(o), hr);
    RET(hr, 1);
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
    g_vt_di = make_vtable("IDirectInput8A", di_methods, 11);
    g_vt_dev = make_vtable("IDirectInputDevice8A", dev_methods, 32);
    tm_loader_add(&g_dll);
}
