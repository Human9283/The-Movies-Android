/*
 * dinput8.dll (src/host/tm_dinput8.c): DirectInput8Create and the COM methods
 * the game calls, through LoadLibraryA/GetProcAddress and the vtables, as the
 * game reaches them. Covers the game's own sequence (sub_009F9950) and the
 * error cases of each implemented method.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } guid_t;
static const guid_t IID_IUnknown_  = {0x00000000, 0x0000, 0x0000, {0xC0,0,0,0,0,0,0,0x46}};
static const guid_t IID_DI8A       = {0xBF798030, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const guid_t IID_DI8W       = {0xBF798031, 0x483A, 0x4DA2, {0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
static const guid_t IID_DID8A      = {0x54D41080, 0xDC15, 0x4833, {0xA4,0x1B,0x74,0x8F,0x73,0xA3,0x81,0x79}};
static const guid_t GUID_SysMouse_ = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0,0}};
static const guid_t GUID_SysKbd_   = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0,0}};
static const guid_t GUID_Joystick_ = {0x6F1D2B70, 0xD5A0, 0x11CF, {0xBF,0xC7,0x44,0x45,0x53,0x54,0,0}};

static uint8_t *g_mem;
static uint32_t g_at;
static uint32_t gput(const void *p, uint32_t n) { uint32_t va = hle_va(g_mem + g_at); memcpy(g_mem + g_at, p, n); g_at += (n + 15) & ~15u; return va; }
static uint32_t gzero(uint32_t n) { uint32_t va = hle_va(g_mem + g_at); memset(g_mem + g_at, 0, n); g_at += (n + 15) & ~15u; return va; }

/* Call method k of a COM object: stdcall, this first. */
static uint32_t com(uint32_t obj, int k, int n, const uint32_t *args) {
    uint32_t a[8] = { obj };
    for (int i = 0; i < n; i++) a[i + 1] = args[i];
    return hle_call_guest(MEM32(MEM32(obj) + 4u * (uint32_t)k), n + 1, a);
}
enum { QI = 0, ADDREF = 1, RELEASE = 2, CREATEDEVICE = 3 };
enum { SETPROPERTY = 6, ACQUIRE = 7, UNACQUIRE = 8, SETDATAFORMAT = 11, SETCOOP = 13 };

int main(void) {
    recomp_host_init();
    win32hle_register(tm_loader_overrides);
    mach_enter();
    tm_dinput8_init();
    g_mem = hle_alloc(65536);

    uint32_t name = gput("DINPUT8.dll", 12), exp = gput("DirectInput8Create", 19);
    uint32_t a1[1] = { name };
    uint32_t h = hle_call_guest(hle_resolve("tm:LoadLibraryA"), 1, a1);
    check("LoadLibraryA(\"DINPUT8.dll\")", h != 0);
    uint32_t a2[2] = { h, exp };
    uint32_t create = hle_call_guest(hle_resolve("tm:GetProcAddress"), 2, a2);
    check("GetProcAddress(DirectInput8Create)", create != 0);

    uint32_t iid_a = gput(&IID_DI8A, 16), iid_w = gput(&IID_DI8W, 16), iid_unk = gput(&IID_IUnknown_, 16);
    uint32_t iid_dev = gput(&IID_DID8A, 16), kbd_g = gput(&GUID_SysKbd_, 16), mouse_g = gput(&GUID_SysMouse_, 16);
    uint32_t joy_g = gput(&GUID_Joystick_, 16);
    uint32_t out = gzero(4), out2 = gzero(4);

    /* DirectInput8Create: argument errors leave *ppvOut NULL */
    uint32_t c[5] = { 0x00400000u, 0x0800, iid_a, out, 0 };
    c[3] = 0;
    check("NULL ppvOut: E_POINTER", hle_call_guest(create, 5, c) == 0x80004003u);
    c[3] = out;
    MEM32(out) = 0xDEADBEEF; c[1] = 0x0700;
    check("old version", hle_call_guest(create, 5, c) == 0x8007047Eu && MEM32(out) == 0);
    c[1] = 0x0900;
    check("newer version", hle_call_guest(create, 5, c) == 0x80070481u && MEM32(out) == 0);
    c[1] = 0x0800; c[0] = 0;
    check("NULL instance: DIERR_INVALIDPARAM", hle_call_guest(create, 5, c) == 0x80070057u && MEM32(out) == 0);
    c[0] = 0x00400000u; c[4] = 0x1234;
    check("aggregation: DIERR_NOAGGREGATION", hle_call_guest(create, 5, c) == 0x80040110u && MEM32(out) == 0);
    c[4] = 0; c[2] = iid_w;
    check("IDirectInput8W: not implemented (E_NOINTERFACE)", hle_call_guest(create, 5, c) == 0x80004002u && MEM32(out) == 0);
    c[2] = iid_dev;
    check("a wrong interface: E_NOINTERFACE", hle_call_guest(create, 5, c) == 0x80004002u && MEM32(out) == 0);

    /* the game's call */
    c[2] = iid_a;
    check("DirectInput8Create(hinst, 0x800, IID_IDirectInput8A)", hle_call_guest(create, 5, c) == 0 && MEM32(out) != 0);
    uint32_t di = MEM32(out);

    /* IUnknown */
    uint32_t q[2] = { iid_unk, out2 };
    check("QI IUnknown", com(di, QI, 2, q) == 0 && MEM32(out2) == di);
    check("refs after QI: Release -> 1", com(di, RELEASE, 0, NULL) == 1);
    q[0] = iid_dev;
    check("QI wrong IID: E_NOINTERFACE, *ppv NULL", com(di, QI, 2, q) == 0x80004002u && MEM32(out2) == 0);
    check("AddRef -> 2", com(di, ADDREF, 0, NULL) == 2);
    check("Release -> 1", com(di, RELEASE, 0, NULL) == 1);

    /* CreateDevice */
    uint32_t cd[3] = { joy_g, out2, 0 };
    MEM32(out2) = 0xDEADBEEF;
    check("joystick: DIERR_DEVICENOTREG, *out NULL", com(di, CREATEDEVICE, 3, cd) == 0x80040154u && MEM32(out2) == 0);
    cd[0] = kbd_g; cd[2] = 0x5678;
    check("aggregation: DIERR_NOAGGREGATION", com(di, CREATEDEVICE, 3, cd) == 0x80040110u);
    cd[2] = 0; cd[1] = 0;
    check("NULL out: E_POINTER", com(di, CREATEDEVICE, 3, cd) == 0x80004003u);
    cd[1] = out2;
    check("CreateDevice(GUID_SysKeyboard)", com(di, CREATEDEVICE, 3, cd) == 0 && MEM32(out2));
    uint32_t kbd = MEM32(out2);
    check("the device holds the DirectInput object: AddRef -> 3", com(di, ADDREF, 0, NULL) == 3);
    com(di, RELEASE, 0, NULL);

    /* device: Acquire before a data format */
    check("Acquire without data format: DIERR_INVALIDPARAM", com(kbd, ACQUIRE, 0, NULL) == 0x80070057u);
    /* c_dfDIKeyboard-shaped format: 256 one-byte key objects */
    uint32_t objs = gzero(256 * 16);
    for (uint32_t k = 0; k < 256; k++) MEM32(objs + 16 * k + 4) = k, MEM32(objs + 16 * k + 8) = 0x8000000Cu | (k << 8);
    uint32_t df_vals[6] = { 24, 16, 2, 256, 256, objs };
    uint32_t df = gput(df_vals, 24);
    uint32_t bad_vals[6] = { 20, 16, 2, 256, 256, objs };
    uint32_t bad = gput(bad_vals, 24);
    uint32_t a[3] = { bad };
    check("SetDataFormat wrong dwSize: DIERR_INVALIDPARAM", com(kbd, SETDATAFORMAT, 1, a) == 0x80070057u);
    a[0] = 0;
    check("SetDataFormat NULL: E_POINTER", com(kbd, SETDATAFORMAT, 1, a) == 0x80004003u);
    a[0] = df;
    check("SetDataFormat(c_dfDIKeyboard)", com(kbd, SETDATAFORMAT, 1, a) == 0);

    /* SetCooperativeLevel */
    a[0] = 0; a[1] = 5;
    check("NULL hwnd, EXCLUSIVE|FOREGROUND: E_HANDLE (the game's call before its window exists)",
          com(kbd, SETCOOP, 2, a) == 0x80070006u);
    a[1] = 3;
    check("both EXCLUSIVE and NONEXCLUSIVE: DIERR_INVALIDPARAM", com(kbd, SETCOOP, 2, a) == 0x80070057u);
    a[1] = 0x2 | 0x4 | 0x8;
    check("both FOREGROUND and BACKGROUND: DIERR_INVALIDPARAM", com(kbd, SETCOOP, 2, a) == 0x80070057u);
    a[0] = 0x12345678; a[1] = 0x2 | 0x8;
    check("not a window: E_HANDLE", com(kbd, SETCOOP, 2, a) == 0x80070006u);
    a[0] = 0; a[1] = 0x2 | 0x8;
    check("NULL hwnd, NONEXCLUSIVE|BACKGROUND: the desktop, DI_OK", com(kbd, SETCOOP, 2, a) == 0);

    /* SetProperty(DIPROP_BUFFERSIZE) */
    uint32_t pd_vals[5] = { 20, 16, 0, 0, 128 };
    uint32_t pd = gput(pd_vals, 20);
    uint32_t pbad_vals[5] = { 20, 16, 0, 1, 128 };          /* DIPH_BYOFFSET: not for a device property */
    uint32_t pbad = gput(pbad_vals, 20);
    a[0] = 1; a[1] = pbad;
    check("SetProperty BUFFERSIZE not DIPH_DEVICE: DIERR_INVALIDPARAM", com(kbd, SETPROPERTY, 2, a) == 0x80070057u);
    a[1] = 0;
    check("SetProperty NULL header: DIERR_INVALIDPARAM", com(kbd, SETPROPERTY, 2, a) == 0x80070057u);
    a[1] = pd;
    check("SetProperty(DIPROP_BUFFERSIZE, 128)", com(kbd, SETPROPERTY, 2, a) == 0);

    /* Acquire / Unacquire */
    check("Acquire", com(kbd, ACQUIRE, 0, NULL) == 0);
    check("Acquire again: DI_NOEFFECT", com(kbd, ACQUIRE, 0, NULL) == 1);
    a[0] = df;
    check("SetDataFormat while acquired: DIERR_ACQUIRED", com(kbd, SETDATAFORMAT, 1, a) == 0x800700AAu);
    a[0] = 0; a[1] = 0x2 | 0x8;
    check("SetCooperativeLevel while acquired: DIERR_ACQUIRED", com(kbd, SETCOOP, 2, a) == 0x800700AAu);
    a[0] = 1; a[1] = pd;
    check("SetProperty while acquired: DIERR_ACQUIRED", com(kbd, SETPROPERTY, 2, a) == 0x800700AAu);
    check("Unacquire", com(kbd, UNACQUIRE, 0, NULL) == 0);
    check("Unacquire again: DI_NOEFFECT", com(kbd, UNACQUIRE, 0, NULL) == 1);

    /* device QI and lifetime */
    q[0] = iid_dev; q[1] = out;
    check("device QI IDirectInputDevice8A", com(kbd, QI, 2, q) == 0 && MEM32(out) == kbd);
    check("device Release -> 1", com(kbd, RELEASE, 0, NULL) == 1);
    check("device Release -> 0 (destroyed)", com(kbd, RELEASE, 0, NULL) == 0);
    check("the device released its DirectInput object: AddRef -> 2", com(di, ADDREF, 0, NULL) == 2);
    com(di, RELEASE, 0, NULL);

    /* the mouse, as the game creates it */
    cd[0] = mouse_g; cd[1] = out2;
    check("CreateDevice(GUID_SysMouse)", com(di, CREATEDEVICE, 3, cd) == 0 && MEM32(out2));
    uint32_t mouse = MEM32(out2);
    a[0] = 0x12345678; a[1] = 0x1 | 0x8;
    check("mouse EXCLUSIVE|BACKGROUND on a bad hwnd: E_HANDLE first", com(mouse, SETCOOP, 2, a) == 0x80070006u);
    check("mouse Release -> 0", com(mouse, RELEASE, 0, NULL) == 0);
    check("DirectInput Release -> 0", com(di, RELEASE, 0, NULL) == 0);

    /* an unimplemented method stops the run, naming itself: not called here */
    check("unimplemented slots are named stubs", hle_name(MEM32(MEM32(kbd) + 4u * 9u)) &&
          !strcmp(hle_name(MEM32(MEM32(kbd) + 4u * 9u)), "IDirectInputDevice8A::GetDeviceState"));

    mach_leave();
    printf("dinput8: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
