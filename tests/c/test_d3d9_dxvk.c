/*
 * The Direct3D 9 backend on DXVK Native (src/host/tm_d3d9_dxvk.cpp), driven
 * only through guest objects and their vtables, as the game drives it:
 * the IDirect3D9 queries DXVK answers, CreateDevice on a guest window, COM
 * identity and reference counts, the guest shadows of buffer and texture
 * locks, a frame cleared and read back through the guest interfaces, and
 * the stops for what is not implemented. Needs a display (xvfb-run) and a
 * Vulkan 1.3 driver. No game files.
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
#include "tm_d3d9_dxvk.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}

static uint8_t *g_mem;
static uint32_t g_at;
static uint32_t gput(const void *p, uint32_t n) { uint32_t va = hle_va(g_mem + g_at); memcpy(g_mem + g_at, p, n); g_at += (n + 15) & ~15u; return va; }
static uint32_t gzero(uint32_t n) { uint32_t va = hle_va(g_mem + g_at); memset(g_mem + g_at, 0, n); g_at += (n + 15) & ~15u; return va; }
static uint32_t api(const char *name, int n, const uint32_t *a) { return hle_call_guest(hle_resolve(name), n, a); }
static uint32_t com(uint32_t obj, int k, int n, const uint32_t *args) {
    uint32_t a[12] = { obj };
    for (int i = 0; i < n; i++) a[i + 1] = args[i];
    return hle_call_guest(MEM32(MEM32(obj) + 4u * (uint32_t)k), n + 1, a);
}
static int in_guest(uint32_t va) { return va >= 0x10000u && va < 0x7D000000u; }

/* vtable slots (d3d9.h) */
enum { QI = 0, ADDREF = 1, RELEASE = 2 };
enum { D3D_GETADAPTERIDENTIFIER = 5, D3D_CHECKDEVICETYPE = 9, D3D_GETDEVICECAPS = 14, D3D_CREATEDEVICE = 16 };
enum { DEV_GETDIRECT3D = 6, DEV_GETDEVICECAPS = 7, DEV_PRESENT = 17, DEV_GETBACKBUFFER = 18, DEV_CREATETEXTURE = 23,
       DEV_CREATEVERTEXBUFFER = 26, DEV_GETRENDERTARGETDATA = 32, DEV_CREATEOFFSCREEN = 36, DEV_GETRENDERTARGET = 38,
       DEV_BEGINSCENE = 41, DEV_ENDSCENE = 42, DEV_CLEAR = 43, DEV_DRAWRECTPATCH = 115 };
enum { TEX_GETSURFACELEVEL = 18, TEX_LOCKRECT = 19, TEX_UNLOCKRECT = 20 };
enum { SURF_GETDEVICE = 3, SURF_GETDESC = 12, SURF_LOCKRECT = 13, SURF_UNLOCKRECT = 14 };
enum { VB_LOCK = 11, VB_UNLOCK = 12, VB_GETDESC = 13 };
enum { K_DEVICE = 0, K_SURFACE = 2, K_TEXTURE = 4, K_VERTEXBUFFER = 7 };

static const uint8_t IID_IUnknown_[16] = { 0,0,0,0, 0,0, 0,0, 0xC0,0,0,0,0,0,0,0x46 };
static const uint8_t IID_IDirect3D9_[16] = { 0xCA,0xCB,0xBD,0x81, 0xD4,0x64, 0x6D,0x42, 0xAE,0x8D,0xAD,0x01,0x47,0xF4,0x27,0x5C };
static const uint8_t IID_IDirect3DDevice9_[16] = { 0x96,0x3B,0x22,0xD0, 0x7A,0xBF, 0xFD,0x43, 0x92,0xBD,0xA4,0x3B,0x0D,0x82,0xB9,0xEB };
static const uint8_t IID_IDirect3DResource9_[16] = { 0x5D,0xC0,0xEE,0x05, 0x7D,0x8F, 0x62,0x43, 0xB9,0x99,0xD1,0xBA,0xF3,0x57,0xC7,0x04 };

static uint32_t g_dev, g_d3d;
static int stops(void (*f)(void)) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) { int dn = open("/dev/null", O_WRONLY); if (dn >= 0) dup2(dn, 2); f(); _exit(0); }
    int st = 0;
    waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st);
}
static void call_unimplemented(void) { uint32_t a[3] = { 0, 0, 0 }; com(g_dev, DEV_DRAWRECTPATCH, 3, a); }
static void call_on_dead(void) { com(g_dev, DEV_BEGINSCENE, 0, NULL); }
static uint32_t g_vb;
static void lock_twice(void) {
    uint32_t p = gzero(4), l[4] = { 0, 4, p, 0 };
    com(g_vb, VB_LOCK, 4, l);
}
static void bad_this(void) {
    uint32_t a[1] = { 0x12345678 };
    hle_call_guest(MEM32(MEM32(g_dev) + 4u * DEV_BEGINSCENE), 1, a);
}

int main(void) {
    recomp_host_init();
    win32hle_register(tm_loader_overrides);
    tm_d3d9_init();
    hle_screen_gl = 2;
    if (hle_screen_open("test_d3d9_dxvk", 0, 0) != 0) { fprintf(stderr, "no window (xvfb-run?)\n"); return 1; }
    tm_d3d9_set_backend(tm_d3d9_dxvk_backend());
    mach_enter();
    g_mem = hle_alloc(1u << 20);

    /* a guest top-level window, as the game's */
    uint32_t cls_name = gput("TestD3D", 8);
    uint32_t wc[10] = { 0, hle_resolve("DefWindowProcA"), 0, 0, 0x00400000u, 0, 0, 0, 0, cls_name };
    uint32_t a1[1] = { gput(wc, 40) };
    check("RegisterClassA", api("RegisterClassA", 1, a1) != 0);
    uint32_t cw[12] = { 0, cls_name, gput("t", 2), 0x80000000u /* WS_POPUP */, 0, 0, 640, 480, 0, 0, 0x00400000u, 0 };
    uint32_t hwnd = api("CreateWindowExA", 12, cw);
    check("CreateWindowExA: a guest window", hwnd != 0);

    uint32_t sdk[1] = { 32 };
    g_d3d = api("d3d9.dll!Direct3DCreate9", 1, sdk);
    check("Direct3DCreate9: a guest IDirect3D9", in_guest(g_d3d));
    uint32_t out = gzero(4), out2 = gzero(4);

    /* ---- IDirect3D9 queries DXVK answers ---- */
    uint32_t id = gzero(1104), ia[3] = { 0, 0, id };
    check("GetAdapterIdentifier: S_OK, a description from DXVK in guest memory",
          com(g_d3d, D3D_GETADAPTERIDENTIFIER, 3, ia) == 0 && ((char *)hle_ptr(id + 512))[0] != 0);
    uint32_t ct[5] = { 0, 1, 22, 22, 1 };
    check("CheckDeviceType(X8R8G8B8, windowed): D3D_OK", com(g_d3d, D3D_CHECKDEVICETYPE, 5, ct) == 0);
    uint32_t caps = gzero(304), gc[3] = { 0, 1, caps };
    check("IDirect3D9::GetDeviceCaps: S_OK, DeviceType HAL", com(g_d3d, D3D_GETDEVICECAPS, 3, gc) == 0 && MEM32(caps) == 1);

    /* ---- CreateDevice ---- */
    uint32_t pp_vals[14] = { 640, 480, 22, 0 /* count 0: Direct3D makes it 1 */, 0, 0, 1, 0, 1 /* windowed */, 1, 80, 0, 0, 0 };
    uint32_t pp = gput(pp_vals, 56);
    uint32_t cd[6] = { 0, 1, hwnd, 0x44, pp, out };
    uint32_t hr = com(g_d3d, D3D_CREATEDEVICE, 6, cd);
    g_dev = MEM32(out);
    check("CreateDevice(640x480 X8R8G8B8 windowed, D16, HARDWARE_VP|MULTITHREADED): S_OK", hr == 0 && g_dev);
    check("  the device is a guest object with a guest vtable", in_guest(g_dev) && in_guest(MEM32(g_dev)));
    check("  BackBufferCount 0 written back as 1", MEM32(pp + 12) == 1);
    check("  hDeviceWindow left as the guest gave it", MEM32(pp + 28) == 0);
    check("  one live device", tm_d3d9_dxvk_live(K_DEVICE) == 1);
    uint32_t cd0[6] = { 0, 1, 0, 0x44, gput(pp_vals, 56), out2 };
    check("CreateDevice with no window at all: D3DERR_INVALIDCALL, *out NULL",
          com(g_d3d, D3D_CREATEDEVICE, 6, cd0) == 0x8876086Cu && MEM32(out2) == 0);

    /* ---- COM identity and counts ---- */
    uint32_t q[2] = { gput(IID_IUnknown_, 16), out2 };
    check("device QI(IUnknown): itself", com(g_dev, QI, 2, q) == 0 && MEM32(out2) == g_dev);
    q[0] = gput(IID_IDirect3DDevice9_, 16);
    check("device QI(IDirect3DDevice9): itself", com(g_dev, QI, 2, q) == 0 && MEM32(out2) == g_dev);
    q[0] = gput(IID_IDirect3D9_, 16);
    check("device QI(IDirect3D9): E_NOINTERFACE, NULL", com(g_dev, QI, 2, q) == 0x80004002u && MEM32(out2) == 0);
    check("refs: two QIs, AddRef -> 4, Release -> 3", com(g_dev, ADDREF, 0, NULL) == 4 && com(g_dev, RELEASE, 0, NULL) == 3);
    com(g_dev, RELEASE, 0, NULL), com(g_dev, RELEASE, 0, NULL);
    uint32_t gd[1] = { out2 };
    check("GetDirect3D: the guest IDirect3D9 that made it", com(g_dev, DEV_GETDIRECT3D, 1, gd) == 0 && MEM32(out2) == g_d3d);
    check("  with a reference: Release -> 1", com(g_d3d, RELEASE, 0, NULL) == 1);
    uint32_t dcaps = gzero(304), dc[1] = { dcaps };
    check("device GetDeviceCaps: written to guest memory", com(g_dev, DEV_GETDEVICECAPS, 1, dc) == 0 && MEM32(dcaps) == 1);

    uint32_t rt1 = gzero(4), rt2 = gzero(4);
    uint32_t gr[2] = { 0, rt1 };
    com(g_dev, DEV_GETRENDERTARGET, 2, gr);
    gr[1] = rt2;
    com(g_dev, DEV_GETRENDERTARGET, 2, gr);
    check("GetRenderTarget twice: the same guest surface", in_guest(MEM32(rt1)) && MEM32(rt1) == MEM32(rt2));
    uint32_t rt = MEM32(rt1);
    q[0] = gput(IID_IDirect3DResource9_, 16);
    check("surface QI(IDirect3DResource9): itself", com(rt, QI, 2, q) == 0 && MEM32(out2) == rt);
    com(rt, RELEASE, 0, NULL);
    uint32_t sg[1] = { out2 };
    check("surface GetDevice: the guest device", com(rt, SURF_GETDEVICE, 1, sg) == 0 && MEM32(out2) == g_dev);
    com(g_dev, RELEASE, 0, NULL);
    uint32_t desc = gzero(32), sd[1] = { desc };
    check("surface GetDesc: 640x480 X8R8G8B8, a render target",
          com(rt, SURF_GETDESC, 1, sd) == 0 && MEM32(desc) == 22 && MEM32(desc + 8) == 1 && MEM32(desc + 24) == 640 && MEM32(desc + 28) == 480);
    com(rt, RELEASE, 0, NULL), com(rt, RELEASE, 0, NULL);

    /* ---- a vertex buffer: Lock hands out guest memory ---- */
    uint32_t vb_out = gzero(4), cv[6] = { 64, 0x8 /* WRITEONLY */, 0, 0 /* DEFAULT */, vb_out, 0 };
    check("CreateVertexBuffer(64, WRITEONLY, DEFAULT)", com(g_dev, DEV_CREATEVERTEXBUFFER, 6, cv) == 0 && in_guest(MEM32(vb_out)));
    uint32_t vb = MEM32(vb_out), ptr = gzero(4);
    uint32_t lk[4] = { 16, 32, ptr, 0 };
    check("Lock(16, 32): a guest pointer", com(vb, VB_LOCK, 4, lk) == 0 && in_guest(MEM32(ptr)));
    for (uint32_t i = 0; i < 32; i++) MEM8(MEM32(ptr) + i) = (uint8_t)(0xA0 + i);
    g_vb = vb;
    check("  a second Lock before Unlock stops the run (not handled)", stops(lock_twice));
    check("Unlock", com(vb, VB_UNLOCK, 0, NULL) == 0);
    check("Unlock again: D3DERR_INVALIDCALL", com(vb, VB_UNLOCK, 0, NULL) == 0x8876086Cu);
    uint32_t lr[4] = { 0, 0, ptr, 0x10 /* READONLY */ };
    int same = com(vb, VB_LOCK, 4, lr) == 0;
    for (uint32_t i = 0; same && i < 32; i++) same = MEM8(MEM32(ptr) + 16 + i) == (uint8_t)(0xA0 + i);
    check("Lock(READONLY) of all: the bytes written, as DXVK holds them", same);
    com(vb, VB_UNLOCK, 0, NULL);
    uint32_t lbad[4] = { 48, 32, ptr, 0 };
    check("Lock past the end: D3DERR_INVALIDCALL, NULL", com(vb, VB_LOCK, 4, lbad) == 0x8876086Cu && MEM32(ptr) == 0);
    uint32_t vdesc = gzero(24), vd[1] = { vdesc };
    check("GetDesc: 64 bytes, WRITEONLY", com(vb, VB_GETDESC, 1, vd) == 0 && MEM32(vdesc + 16) == 64 && MEM32(vdesc + 8) == 8);
    check("Release: freed", com(vb, RELEASE, 0, NULL) == 0 && tm_d3d9_dxvk_live(K_VERTEXBUFFER) == 0);

    /* ---- a texture: LockRect hands out a guest shadow ---- */
    uint32_t tx_out = gzero(4), ctx[8] = { 8, 8, 1, 0, 21 /* A8R8G8B8 */, 1 /* MANAGED */, tx_out, 0 };
    check("CreateTexture(8x8 A8R8G8B8, MANAGED)", com(g_dev, DEV_CREATETEXTURE, 8, ctx) == 0 && in_guest(MEM32(tx_out)));
    uint32_t tx = MEM32(tx_out), lrect = gzero(8);
    uint32_t tl[4] = { 0, lrect, 0, 0 };
    check("LockRect(0): pitch and guest bits", com(tx, TEX_LOCKRECT, 4, tl) == 0 && (int32_t)MEM32(lrect) >= 32 && in_guest(MEM32(lrect + 4)));
    uint32_t pitch = MEM32(lrect), bits = MEM32(lrect + 4);
    for (uint32_t y = 0; y < 8; y++) for (uint32_t x = 0; x < 8; x++) MEM32(bits + y * pitch + 4 * x) = 0xFF000000u | y << 8 | x;
    uint32_t tu[1] = { 0 };
    check("UnlockRect(0)", com(tx, TEX_UNLOCKRECT, 1, tu) == 0);
    tl[3] = 0x10;
    same = com(tx, TEX_LOCKRECT, 4, tl) == 0;
    pitch = MEM32(lrect), bits = MEM32(lrect + 4);
    for (uint32_t y = 0; same && y < 8; y++) for (uint32_t x = 0; same && x < 8; x++) same = MEM32(bits + y * pitch + 4 * x) == (0xFF000000u | y << 8 | x);
    check("LockRect(READONLY): the texels written, from DXVK", same);
    com(tx, TEX_UNLOCKRECT, 1, tu);
    uint32_t rect = gput((uint32_t[4]){ 2, 3, 6, 5 }, 16);
    uint32_t tl2[4] = { 0, lrect, rect, 0 };
    same = com(tx, TEX_LOCKRECT, 4, tl2) == 0;
    bits = MEM32(lrect + 4);
    same = same && MEM32(bits) == (0xFF000000u | 3 << 8 | 2);   /* the rectangle's first texel */
    MEM32(bits) = 0xFFABCDEFu;
    com(tx, TEX_UNLOCKRECT, 1, tu);
    tl[3] = 0x10;
    com(tx, TEX_LOCKRECT, 4, tl);
    pitch = MEM32(lrect), bits = MEM32(lrect + 4);
    same = same && MEM32(bits + 3 * pitch + 8) == 0xFFABCDEFu && MEM32(bits + 3 * pitch + 4) == (0xFF000000u | 3 << 8 | 1);
    com(tx, TEX_UNLOCKRECT, 1, tu);
    check("LockRect of a rectangle: its texels, written back in place only", same);
    uint32_t lvl = gzero(4), gl[2] = { 0, lvl };
    check("GetSurfaceLevel(0): a guest surface", com(tx, TEX_GETSURFACELEVEL, 2, gl) == 0 && in_guest(MEM32(lvl)));
    int surf_before = tm_d3d9_dxvk_live(K_SURFACE);
    check("  its Release counts on the texture (2 -> 1)", com(MEM32(lvl), RELEASE, 0, NULL) == 1);
    check("texture Release -> 0: the texture and its level go", com(tx, RELEASE, 0, NULL) == 0 &&
          tm_d3d9_dxvk_live(K_TEXTURE) == 0 && tm_d3d9_dxvk_live(K_SURFACE) == surf_before - 1);
    uint32_t dxt[8] = { 8, 8, 1, 0, 0x31545844 /* DXT1 */, 1, tx_out, 0 };
    check("CreateTexture(8x8 DXT1)", com(g_dev, DEV_CREATETEXTURE, 8, dxt) == 0);
    tx = MEM32(tx_out);
    tl[3] = 0;
    check("DXT1 LockRect: a pitch of 2 blocks (16 bytes) or more", com(tx, TEX_LOCKRECT, 4, tl) == 0 && MEM32(lrect) >= 16);
    com(tx, TEX_UNLOCKRECT, 1, tu);
    com(tx, RELEASE, 0, NULL);

    /* ---- a frame: Clear, Present, read back through the guest interfaces ---- */
    check("BeginScene", com(g_dev, DEV_BEGINSCENE, 0, NULL) == 0);
    uint32_t z = 0x3F800000u;   /* 1.0f */
    uint32_t cl[6] = { 0, 0, 0x3, 0xFF2080C0u, z, 0 };
    check("Clear(target | z, 0x2080C0, 1.0)", com(g_dev, DEV_CLEAR, 6, cl) == 0);
    check("EndScene", com(g_dev, DEV_ENDSCENE, 0, NULL) == 0);
    uint32_t bb = gzero(4), gb[4] = { 0, 0, 0, bb };
    check("GetBackBuffer(0): a guest surface", com(g_dev, DEV_GETBACKBUFFER, 4, gb) == 0 && in_guest(MEM32(bb)));
    uint32_t sys = gzero(4), co[6] = { 640, 480, 22, 2 /* SYSTEMMEM */, sys, 0 };
    check("CreateOffscreenPlainSurface(SYSTEMMEM)", com(g_dev, DEV_CREATEOFFSCREEN, 6, co) == 0);
    uint32_t grd[2] = { MEM32(bb), MEM32(sys) };
    check("GetRenderTargetData(back buffer -> system surface)", com(g_dev, DEV_GETRENDERTARGETDATA, 2, grd) == 0);
    uint32_t sl[3] = { lrect, 0, 0x10 };
    uint32_t px = 0;
    if (com(MEM32(sys), SURF_LOCKRECT, 3, sl) == 0) {
        px = MEM32(MEM32(lrect + 4) + 240 * MEM32(lrect) + 4 * 320);
        com(MEM32(sys), SURF_UNLOCKRECT, 0, NULL);
    }
    check("  the centre pixel is the clear colour", (px & 0xFFFFFF) == 0x2080C0);
    uint32_t pr[4] = { 0, 0, 0, 0 };
    check("Present", com(g_dev, DEV_PRESENT, 4, pr) == 0);
    com(MEM32(sys), RELEASE, 0, NULL);
    com(MEM32(bb), RELEASE, 0, NULL);

    /* ---- what stops the run ---- */
    check("an unimplemented method (DrawRectPatch) stops the run", stops(call_unimplemented));
    check("a method with a `this` that is no device stops the run", stops(bad_this));

    check("device Release -> 0: freed", com(g_dev, RELEASE, 0, NULL) == 0 && tm_d3d9_dxvk_live(K_DEVICE) == 0);
    check("a method after the last Release stops the run", stops(call_on_dead));
    check("IDirect3D9 Release -> 0", com(g_d3d, RELEASE, 0, NULL) == 0 && tm_d3d9_live() == 0);
    mach_leave();
    printf("d3d9_dxvk: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
