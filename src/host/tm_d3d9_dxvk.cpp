/*
 * tm_d3d9_dxvk.cpp - the Direct3D 9 device backend on DXVK Native
 * (docs/DXVK-INTEGRATION.md): guest Direct3D 9 objects in guest memory, each
 * standing for a DXVK object on the host.
 *
 *   guest IDirect3D9 (tm_d3d9.c) --CreateDevice--> this file
 *     -> IDirect3D9 of DXVK (one per guest IDirect3D9, made on first use)
 *     -> IDirect3DDevice9 of DXVK, on win32hle's SDL window (Vulkan mode)
 *     -> its surfaces, textures, buffers, shaders... (tm_d3d9_ifaces.h)
 *
 * The guest sees only 16-byte objects whose first dword is a vtable of shim
 * VAs, both in guest memory. Host objects are found in tables keyed by the
 * guest address, and a host object has one guest object, so the same DXVK
 * object reaches the guest as the same address while the guest holds it.
 * No host pointer is written to guest memory.
 *
 * Reference counts are DXVK's: AddRef and Release are forwarded, and a guest
 * object is freed when DXVK's public count reaches 0.
 *
 * Method status (docs/D3D9-REQUIREMENTS.md 6): QueryInterface, AddRef and
 * Release of every interface, and the methods in k_impl, are implemented.
 * Every other method is a diagnostic stop: it prints the method, its
 * arguments as the guest passed them and the lifted function that called
 * it, then ends the run. A method is implemented when the game is seen to
 * call it, with a test (tests/c/test_d3d9_dxvk.c).
 */
extern "C" {
#include "win32hle.h"
}
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <d3d9.h>
#include "tm_d3d9_backend.h"
#include "tm_d3d9_dxvk.h"
#include "tm_d3d9_ifaces.h"

using namespace tmd3d;

namespace {

static_assert(k_if[DEVICE].n == 119, "IDirect3DDevice9 has 119 methods");
static_assert(sizeof(Guid) == 16 && sizeof(GUID) == 16, "GUID layout");

constexpr uint32_t S_OK_ = 0, E_NOINTERFACE_ = 0x80004002u, E_POINTER_ = 0x80004003u;
constexpr uint32_t D3DERR_INVALIDCALL_ = 0x8876086Cu, D3DERR_NOTAVAILABLE_ = 0x8876086Au;

/* ---- guest objects ---- */
/* A guest object. A buffer the guest locks has a shadow: a block of guest
 * memory the size of the buffer, which Lock hands out (docs/DXVK-INTEGRATION.md 4). */
struct Proxy {
    IUnknown *host;
    Kind kind;
    uint32_t parent = 0;                                 /* a device: the guest IDirect3D9 that made it;
                                                            a texture's level: the texture */
    uint32_t shadow = 0, shadow_size = 0;                /* guest VA and size, once locked */
    int locks = 0;
    uint32_t lock_off = 0, lock_size = 0, lock_flags = 0;
};
std::unordered_map<uint32_t, Proxy> g_px;               /* guest object -> DXVK's */
std::unordered_map<IUnknown *, uint32_t> g_rev;         /* DXVK's -> guest object */
std::unordered_map<uint32_t, IDirect3D9 *> g_d3d;      /* guest IDirect3D9 -> DXVK's */
uint32_t g_vt[NKIND];
int g_made[NKIND], g_freed[NKIND], g_create_failed;

/* the lifted function that called the method being run (the lifted code
 * pushes no real return address: RECOMP_RETADDR) */
uint32_t caller() { return g_cur_func; }

bool same(const uint8_t *g, const Guid &id) { return !memcmp(g, &id, 16); }

uint32_t vtable(Kind k);

/* A host object the guest now holds a reference to (the one DXVK returned
 * with it): its guest object, made on first sight. */
uint32_t wrap(IUnknown *h, Kind k) {
    if (!h) return 0;
    auto it = g_rev.find(h);
    if (it != g_rev.end()) {
        if (g_px[it->second].kind != k)
            hle_fatal("[d3d9] a DXVK object reached the guest as %s and %s", k_if[g_px[it->second].kind].name, k_if[k].name);
        return it->second;
    }
    uint32_t *o = (uint32_t *)hle_alloc(16);
    if (!o) hle_fatal("[d3d9] out of guest memory for an %s", k_if[k].name);
    o[0] = vtable(k);
    uint32_t va = hle_va(o);
    g_px[va] = Proxy{ h, k };
    g_rev[h] = va;
    g_made[k]++;
    return va;
}

/* `this` of the method being run, of kind k */
template <class T> T *self(Kind k, const char *method) {
    uint32_t va = A32(0);
    auto it = g_px.find(va);
    if (it == g_px.end() || it->second.kind != k)
        hle_fatal("[d3d9] %s::%s on 0x%08X: not a live %s", k_if[k].name, method, va, k_if[k].name);
    return static_cast<T *>(it->second.host);
}

/* An object the guest passes in: NULL, or a live object of one of the kinds
 * in `mask` (a bit per Kind). */
template <class T> T *in(uint32_t va, unsigned mask, const char *what) {
    if (!va) return nullptr;
    auto it = g_px.find(va);
    if (it == g_px.end() || !(mask & (1u << it->second.kind)))
        hle_fatal("[d3d9] %s: 0x%08X is not a live object of the expected interface", what, va);
    return static_cast<T *>(it->second.host);
}

/* ---- generic forwarding: a guest call to the DXVK method M ----
 *
 * Each argument is converted by its host type (Conv<T>):
 *   integers, enums, BOOL: the dword;  float: the dword's bits;
 *   a pointer to a structure that holds no pointer (Pod<T>, sizes checked
 *     below against the 32-bit layout): the guest address, which the host
 *     can read and write directly;
 *   a pointer to an interface: the live guest object's DXVK object;
 *   a pointer to an interface pointer (an object returned): a local the
 *     method fills, then its guest object is written to the guest;
 *   HANDLE * (shared resources, Windows Vista and later): must be NULL.
 * Anything else does not compile. The argument count is checked against
 * the vtable table (tm_d3d9_ifaces.h). */
template <class T> struct Pod : std::false_type {};
#define TM_POD(T, size) static_assert(sizeof(T) == size, #T " layout"); template <> struct Pod<T> : std::true_type {}
TM_POD(D3DMATRIX, 64); TM_POD(D3DVIEWPORT9, 24); TM_POD(D3DMATERIAL9, 68); TM_POD(D3DLIGHT9, 104);
TM_POD(RECT, 16); TM_POD(D3DRECT, 16); TM_POD(POINT, 8); TM_POD(D3DBOX, 24);
TM_POD(D3DSURFACE_DESC, 32); TM_POD(D3DVOLUME_DESC, 28); TM_POD(D3DVERTEXBUFFER_DESC, 24); TM_POD(D3DINDEXBUFFER_DESC, 20);
TM_POD(D3DDISPLAYMODE, 16); TM_POD(D3DGAMMARAMP, 1536); TM_POD(D3DCLIPSTATUS9, 8); TM_POD(PALETTEENTRY, 4);
TM_POD(D3DVERTEXELEMENT9, 8); TM_POD(D3DRASTER_STATUS, 8); TM_POD(D3DCAPS9, 304);
TM_POD(float, 4); TM_POD(int, 4); TM_POD(DWORD, 4);
#undef TM_POD

template <class I> struct KindOf;
#define TM_KIND(I, mask, out) template <> struct KindOf<I> { static constexpr unsigned in = mask; static constexpr Kind kind = out; }
TM_KIND(IDirect3DDevice9, 1u << DEVICE, DEVICE);
TM_KIND(IDirect3DSwapChain9, 1u << SWAPCHAIN, SWAPCHAIN);
TM_KIND(IDirect3DSurface9, 1u << SURFACE, SURFACE);
TM_KIND(IDirect3DVolume9, 1u << VOLUME, VOLUME);
TM_KIND(IDirect3DTexture9, 1u << TEXTURE, TEXTURE);
TM_KIND(IDirect3DCubeTexture9, 1u << CUBETEXTURE, CUBETEXTURE);
TM_KIND(IDirect3DVolumeTexture9, 1u << VOLUMETEXTURE, VOLUMETEXTURE);
TM_KIND(IDirect3DBaseTexture9, (1u << TEXTURE) | (1u << CUBETEXTURE) | (1u << VOLUMETEXTURE), NKIND);
TM_KIND(IDirect3DVertexBuffer9, 1u << VERTEXBUFFER, VERTEXBUFFER);
TM_KIND(IDirect3DIndexBuffer9, 1u << INDEXBUFFER, INDEXBUFFER);
TM_KIND(IDirect3DVertexDeclaration9, 1u << VERTEXDECL, VERTEXDECL);
TM_KIND(IDirect3DVertexShader9, 1u << VERTEXSHADER, VERTEXSHADER);
TM_KIND(IDirect3DPixelShader9, 1u << PIXELSHADER, PIXELSHADER);
TM_KIND(IDirect3DStateBlock9, 1u << STATEBLOCK, STATEBLOCK);
TM_KIND(IDirect3DQuery9, 1u << QUERY, QUERY);
#undef TM_KIND

/* a base texture's own kind, from DXVK's answer */
Kind texture_kind(IDirect3DBaseTexture9 *t) {
    switch (t->GetType()) {
        case D3DRTYPE_TEXTURE: return TEXTURE;
        case D3DRTYPE_CUBETEXTURE: return CUBETEXTURE;
        case D3DRTYPE_VOLUMETEXTURE: return VOLUMETEXTURE;
        default: hle_fatal("[d3d9] a base texture of resource type %d", (int)t->GetType());
    }
}
template <class I> Kind kind_of(I *) { return KindOf<I>::kind; }
Kind kind_of(IDirect3DBaseTexture9 *t) { return texture_kind(t); }

template <class T, class = void> struct Conv {
    static_assert(sizeof(T) == 0, "no guest conversion for this argument type");
};
template <class T> struct Conv<T, std::enable_if_t<std::is_integral<T>::value || std::is_enum<T>::value>> {
    uint32_t v;
    T get() const { return (T)v; }
    void after() {}
};
template <> struct Conv<float> {
    uint32_t v;
    float get() const { float f; memcpy(&f, &v, 4); return f; }
    void after() {}
};
template <class T> struct Conv<T *, std::enable_if_t<Pod<std::remove_const_t<T>>::value>> {
    uint32_t v;
    T *get() const { return v ? (T *)hle_ptr(v) : nullptr; }
    void after() {}
};
template <> struct Conv<const void *> {                 /* vertex and index data the method reads (the UP draws) */
    uint32_t v;
    const void *get() const { return v ? hle_ptr(v) : nullptr; }
    void after() {}
};
template <> struct Conv<void *> {                       /* data the method writes (IDirect3DQuery9::GetData) */
    uint32_t v;
    void *get() const { return v ? hle_ptr(v) : nullptr; }
    void after() {}
};
template <class I> struct Conv<I *, std::enable_if_t<std::is_base_of<IUnknown, I>::value>> {
    uint32_t v;
    I *get() const { return in<I>(v, KindOf<I>::in, "an argument"); }
    void after() {}
};
template <class I> struct Conv<I **, std::enable_if_t<std::is_base_of<IUnknown, I>::value>> {
    uint32_t v;
    I *obj = nullptr;
    I **get() { if (v) MEM32(v) = 0; return v ? &obj : nullptr; }
    void after() { if (v) MEM32(v) = obj ? wrap(obj, kind_of(obj)) : 0; }
};
template <> struct Conv<HANDLE *> {
    uint32_t v;
    HANDLE *get() const {
        if (v && MEM32(v)) hle_fatal("[d3d9] a shared resource handle (Windows Vista and later) is not supported");
        return nullptr;
    }
    void after() {}
};

/* the slot of a method, found at compile time */
constexpr bool streq(const char *a, const char *b) { return *a == *b && (!*a || streq(a + 1, b + 1)); }
constexpr int slot_of(Kind k, const char *name) {
    for (int s = 0; s < k_if[k].n; s++) if (streq(k_if[k].m[s].name, name)) return s;
    return -1;
}

int g_calls[NKIND][128];
constexpr int k_logged = 3;                              /* calls printed per method; all counted */

template <auto M, int K, int S> struct Fwd;
template <class R, class C, class... A, R (C::*M)(A...), int K, int S> struct Fwd<M, K, S> {
    static_assert(S >= 0, "no such method");
    static_assert((int)sizeof...(A) == k_if[K].m[S].nargs, "argument count differs from the vtable table");
    template <size_t... I> static void run(std::index_sequence<I...>) {
        C *obj = self<C>((Kind)K, k_if[K].m[S].name);
        std::tuple<Conv<A>...> cv{ Conv<A>{ A32((int)I + 1) }... };
        uint32_t rv;
        if constexpr (std::is_void<R>::value) {
            (obj->*M)(std::get<I>(cv).get()...);
            rv = 0;
        } else {
            static_assert(!std::is_floating_point<R>::value, "a float result is returned in ST0, not EAX");
            rv = (uint32_t)(obj->*M)(std::get<I>(cv).get()...);
        }
        (void)std::initializer_list<int>{ (std::get<I>(cv).after(), 0)... };
        if (++g_calls[K][S] <= k_logged) {
            char a[200] = "";
            int at = 0;
            for (int i = 1; i <= (int)sizeof...(A) && at < (int)sizeof a; i++)
                at += snprintf(a + at, sizeof a - (size_t)at, "%s0x%X", i > 1 ? ", " : "", A32(i));
            fprintf(stderr, "[d3d9] %s::%s(%s) -> 0x%08X%s\n", k_if[K].name, k_if[K].m[S].name, a, rv,
                    g_calls[K][S] == k_logged ? " (further calls counted, not printed)" : "");
        }
        RET(rv, 1 + sizeof...(A));
    }
    static void call() { run(std::index_sequence_for<A...>{}); }
};
#define FWD(K, I, NAME) { K, #NAME, &Fwd<&I::NAME, K, slot_of(K, #NAME)>::call }

/* A texture's levels count their references on the texture (as on Windows),
 * so their guest objects never see 0: they go with the texture. */
void forget_levels(uint32_t tex) {
    for (auto it = g_px.begin(); it != g_px.end();)
        if (it->second.kind == SURFACE && it->second.parent == tex) {
            if (it->second.shadow) hle_free(hle_ptr(it->second.shadow));
            g_rev.erase(it->second.host);
            hle_free(hle_ptr(it->first));
            g_freed[SURFACE]++;
            it = g_px.erase(it);
        } else ++it;
}

/* ---- IUnknown, for every interface ---- */
template <int K> void px_QueryInterface() {             /* (this, riid, ppv) */
    self<IUnknown>((Kind)K, "QueryInterface");
    uint32_t riid = A32(1), ppv = A32(2);
    if (!ppv) RET(E_POINTER_, 3);
    MEM32(ppv) = 0;
    if (!riid) RET(E_POINTER_, 3);
    const uint8_t *g = (const uint8_t *)hle_ptr(riid);
    const Iface &f = k_if[K];
    if (same(g, k_iid_unknown) || same(g, f.iid) || ((f.bases & B_RESOURCE) && same(g, k_iid_resource)) ||
        ((f.bases & B_BASETEX) && same(g, k_iid_basetex))) {
        g_px[A32(0)].host->AddRef();
        MEM32(ppv) = A32(0);
        RET(S_OK_, 3);
    }
    char s[40];
    hle_guid_string(g, s);
    fprintf(stderr, "[d3d9] %s::QueryInterface(%s) -> E_NOINTERFACE\n", f.name, s);
    RET(E_NOINTERFACE_, 3);
}
template <int K> void px_AddRef() { RET(self<IUnknown>((Kind)K, "AddRef")->AddRef(), 1); }
template <int K> void px_Release() {
    uint32_t va = A32(0);
    IUnknown *h = self<IUnknown>((Kind)K, "Release");
    ULONG n = h->Release();
    if (!n) {
        if (K == DEVICE) fprintf(stderr, "[d3d9] IDirect3DDevice9 0x%08X destroyed\n", va);
        if (K == TEXTURE || K == CUBETEXTURE) forget_levels(va);
        if (g_px[va].shadow) hle_free(hle_ptr(g_px[va].shadow));
        g_px.erase(va);
        g_rev.erase(h);
        hle_free(hle_ptr(va));
        g_freed[K]++;
    }
    RET(n, 1);
}

/* ---- the methods not implemented: a diagnostic stop each ---- */
template <int K, int N> void stop() {
    char a[320];
    int at = snprintf(a, sizeof a, "this 0x%08X", A32(0));
    for (int i = 1; i <= k_if[K].m[N].nargs && at < (int)sizeof a; i++)
        at += snprintf(a + at, sizeof a - (size_t)at, ", 0x%08X", A32(i));
    hle_fatal("[d3d9] %s::%s(%s), called from sub_%08X: not implemented (docs/D3D9-REQUIREMENTS.md 6)", k_if[K].name,
              k_if[K].m[N].name, a, caller());
}
template <int K, int... N> std::vector<recomp_func_t> stops_of(std::integer_sequence<int, N...>) {
    return { &stop<K, N>... };
}
template <int K> std::vector<recomp_func_t> stops_for() {
    std::vector<recomp_func_t> v = stops_of<K>(std::make_integer_sequence<int, k_if[K].n>{});
    v[0] = px_QueryInterface<K>, v[1] = px_AddRef<K>, v[2] = px_Release<K>;
    return v;
}
template <int... K> std::array<std::vector<recomp_func_t>, NKIND> all_stops(std::integer_sequence<int, K...>) {
    return { { stops_for<K>()... } };
}

/* ---- IDirect3DDevice9 ---- */
/* D3DCAPS9 holds no pointer: the same 304 bytes in the guest and on the host */
static_assert(sizeof(D3DCAPS9) == 304, "D3DCAPS9 layout");
void dev_GetDeviceCaps() {                              /* (this, D3DCAPS9 *) */
    IDirect3DDevice9 *d = self<IDirect3DDevice9>(DEVICE, "GetDeviceCaps");
    uint32_t out = A32(1);
    if (!out) RET(D3DERR_INVALIDCALL_, 2);
    D3DCAPS9 c = {};
    HRESULT hr = d->GetDeviceCaps(&c);
    if (SUCCEEDED(hr)) memcpy(hle_ptr(out), &c, sizeof c);
    if (++g_calls[DEVICE][slot_of(DEVICE, "GetDeviceCaps")] == 1)
    fprintf(stderr, "[d3d9] IDirect3DDevice9::GetDeviceCaps -> 0x%08X: VS %u.%u, PS %u.%u, max texture %ux%u, "
                    "%u simultaneous textures, %u texture stages, %u lights\n", (unsigned)hr,
            (unsigned)D3DSHADER_VERSION_MAJOR(c.VertexShaderVersion), (unsigned)D3DSHADER_VERSION_MINOR(c.VertexShaderVersion),
            (unsigned)D3DSHADER_VERSION_MAJOR(c.PixelShaderVersion), (unsigned)D3DSHADER_VERSION_MINOR(c.PixelShaderVersion),
            (unsigned)c.MaxTextureWidth, (unsigned)c.MaxTextureHeight, (unsigned)c.MaxSimultaneousTextures,
            (unsigned)c.MaxTextureBlendStages, (unsigned)c.MaxActiveLights);
    RET((uint32_t)hr, 2);
}


void dev_GetDirect3D() {                                /* (this, IDirect3D9 **): the guest IDirect3D9 that made it */
    self<IDirect3DDevice9>(DEVICE, "GetDirect3D");
    uint32_t out = A32(1), d3d = g_px[A32(0)].parent;
    if (!out) RET(D3DERR_INVALIDCALL_, 2);
    if (!tm_d3d9_is_d3d(d3d)) hle_fatal("[d3d9] IDirect3DDevice9::GetDirect3D: its IDirect3D9 0x%08X was destroyed", d3d);
    tm_d3d9_addref(d3d);
    MEM32(out) = d3d;
    RET(S_OK_, 2);
}

/* ---- vertex and index buffers: Lock hands out the guest shadow ----
 *
 * The guest is the only writer of a buffer's contents (Direct3D 9 has no GPU
 * writes into a vertex or index buffer but ProcessVertices, which stops the
 * run), so the shadow always holds what DXVK's buffer holds. Lock returns
 * the shadow at the offset; Unlock copies the locked range into DXVK's
 * buffer under a DXVK lock with the guest's flags (DISCARD, NOOVERWRITE).
 * A READONLY lock copies nothing back. Nested locks stop the run. */
constexpr uint32_t LOCK_READONLY = D3DLOCK_READONLY, LOCK_DISCARD = D3DLOCK_DISCARD, LOCK_NOOVERWRITE = D3DLOCK_NOOVERWRITE,
    LOCK_KNOWN = D3DLOCK_READONLY | D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK | D3DLOCK_DONOTWAIT |
                 D3DLOCK_NO_DIRTY_UPDATE;
template <class B, class D, int K> void buffer_Lock() {  /* (this, offset, size, void **data, flags) */
    B *b = self<B>((Kind)K, "Lock");
    Proxy &p = g_px[A32(0)];
    uint32_t off = A32(1), size = A32(2), out = A32(3), flags = A32(4);
    D desc = {};
    b->GetDesc(&desc);
    if (!out) RET(D3DERR_INVALIDCALL_, 5);
    if (flags & ~LOCK_KNOWN) hle_fatal("[d3d9] %s::Lock: flags 0x%X not handled", k_if[K].name, flags);
    if (p.locks) hle_fatal("[d3d9] %s::Lock: a second lock before Unlock is not handled", k_if[K].name);
    if (!size && !off) size = desc.Size;
    if (off > desc.Size || size > desc.Size - off) {
        fprintf(stderr, "[d3d9] %s::Lock(%u, %u) past its %u bytes -> D3DERR_INVALIDCALL\n", k_if[K].name, off, size, desc.Size);
        MEM32(out) = 0;
        RET(D3DERR_INVALIDCALL_, 5);
    }
    if (!p.shadow) {
        void *m = hle_alloc(desc.Size ? desc.Size : 1);
        if (!m) hle_fatal("[d3d9] %s: no guest memory for a %u-byte shadow", k_if[K].name, desc.Size);
        p.shadow = hle_va(m), p.shadow_size = desc.Size;
    }
    if (flags & LOCK_READONLY) {                         /* what DXVK holds, for reading */
        void *h = nullptr;
        HRESULT hr = b->Lock(off, size, &h, flags);
        if (FAILED(hr)) { MEM32(out) = 0; RET((uint32_t)hr, 5); }
        memcpy(hle_ptr(p.shadow + off), h, size);
        b->Unlock();
    }
    p.locks = 1, p.lock_off = off, p.lock_size = size, p.lock_flags = flags;
    MEM32(out) = p.shadow + off;
    if (++g_calls[K][slot_of((Kind)K, "Lock")] <= k_logged)
        fprintf(stderr, "[d3d9] %s::Lock(%u, %u, flags 0x%X) of %u bytes -> shadow 0x%08X\n", k_if[K].name, off, size, flags,
                desc.Size, p.shadow + off);
    RET(S_OK_, 5);
}
template <class B, int K> void buffer_Unlock() {        /* (this) */
    B *b = self<B>((Kind)K, "Unlock");
    Proxy &p = g_px[A32(0)];
    if (!p.locks) RET(D3DERR_INVALIDCALL_, 1);
    p.locks = 0;
    if (!(p.lock_flags & LOCK_READONLY) && p.lock_size) {
        void *h = nullptr;
        HRESULT hr = b->Lock(p.lock_off, p.lock_size, &h, p.lock_flags & (LOCK_DISCARD | LOCK_NOOVERWRITE));
        if (FAILED(hr)) hle_fatal("[d3d9] %s::Unlock: DXVK's Lock failed 0x%08X", k_if[K].name, (unsigned)hr);
        memcpy(h, hle_ptr(p.shadow + p.lock_off), p.lock_size);
        b->Unlock();
    }
    RET(S_OK_, 1);
}

/* ---- surfaces and texture levels: LockRect hands out a guest shadow ----
 *
 * D3DLOCKED_RECT holds a pointer, so DXVK's cannot reach the guest. LockRect
 * locks DXVK's surface with the guest's flags and keeps it locked; the guest
 * gets a shadow in guest memory with DXVK's pitch, filled from DXVK's memory
 * unless the lock is DISCARD. UnlockRect copies the rows back unless the
 * lock is READONLY, then unlocks DXVK's surface and frees the shadow. Rows
 * are copied for the locked rectangle's width only, in 4x4 blocks for DXT. */
struct RectLock { uint32_t shadow; uint8_t *host; int pitch; uint32_t rowbytes, rows; uint32_t flags; };
std::unordered_map<uint64_t, RectLock> g_rect;          /* (guest object, face, level) -> its open lock */
uint64_t rect_key(uint32_t obj, uint32_t face, uint32_t level) { return (uint64_t)obj << 32 | face << 16 | level; }

/* bytes per pixel, or per 4x4 block (*block set) */
uint32_t format_bytes(D3DFORMAT f, bool *block) {
    *block = false;
    switch ((uint32_t)f) {
        case D3DFMT_DXT1: *block = true; return 8;
        case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: *block = true; return 16;
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: case D3DFMT_D24S8:
        case D3DFMT_D24X8: case D3DFMT_D32: case D3DFMT_A2R10G10B10: case D3DFMT_G16R16: return 4;
        case D3DFMT_R8G8B8: return 3;
        case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
        case D3DFMT_A8L8: case D3DFMT_V8U8: case D3DFMT_L16: case D3DFMT_D16: case D3DFMT_D16_LOCKABLE: return 2;
        case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8: case D3DFMT_A4L4: return 1;
        default: hle_fatal("[d3d9] LockRect: the size of format %u is not known here", (unsigned)f);
    }
}

/* D3DLOCKED_RECT in the guest: { INT Pitch; void *pBits (a guest address) } */
template <class LockFn> uint32_t rect_lock(uint64_t key, const char *what, const D3DSURFACE_DESC &d, uint32_t out,
                                           uint32_t rect, uint32_t flags, LockFn lock) {
    if (!out) return D3DERR_INVALIDCALL_;
    if (flags & ~LOCK_KNOWN) hle_fatal("[d3d9] %s: flags 0x%X not handled", what, flags);
    if (g_rect.count(key)) hle_fatal("[d3d9] %s: a second lock before UnlockRect is not handled", what);
    const RECT *r = rect ? (const RECT *)hle_ptr(rect) : nullptr;
    D3DLOCKED_RECT hl = {};
    HRESULT hr = lock(&hl, r, flags);
    if (FAILED(hr)) { MEM32(out) = 0, MEM32(out + 4) = 0; return (uint32_t)hr; }
    bool block;
    uint32_t bytes = format_bytes(d.Format, &block);
    uint32_t w = r ? (uint32_t)(r->right - r->left) : d.Width, h = r ? (uint32_t)(r->bottom - r->top) : d.Height;
    RectLock L;
    L.host = (uint8_t *)hl.pBits, L.pitch = hl.Pitch, L.flags = flags;
    L.rowbytes = block ? (w + 3) / 4 * bytes : w * bytes;
    L.rows = block ? (h + 3) / 4 : h;
    if (L.pitch <= 0 || L.rowbytes > (uint32_t)L.pitch) hle_fatal("[d3d9] %s: pitch %d for rows of %u bytes", what, L.pitch, L.rowbytes);
    uint32_t size = L.rows ? (uint32_t)L.pitch * (L.rows - 1) + L.rowbytes : 1;
    void *m = hle_alloc(size);
    if (!m) hle_fatal("[d3d9] %s: no guest memory for a %u-byte shadow", what, size);
    L.shadow = hle_va(m);
    if (!(flags & LOCK_DISCARD))
        for (uint32_t y = 0; y < L.rows; y++)
            memcpy((uint8_t *)m + (size_t)y * L.pitch, L.host + (size_t)y * L.pitch, L.rowbytes);
    g_rect[key] = L;
    MEM32(out) = (uint32_t)L.pitch, MEM32(out + 4) = L.shadow;
    return S_OK_;
}
template <class UnlockFn> uint32_t rect_unlock(uint64_t key, UnlockFn unlock) {
    auto it = g_rect.find(key);
    if (it == g_rect.end()) return D3DERR_INVALIDCALL_;
    RectLock L = it->second;
    g_rect.erase(it);
    if (!(L.flags & LOCK_READONLY))
        for (uint32_t y = 0; y < L.rows; y++)
            memcpy(L.host + (size_t)y * L.pitch, (uint8_t *)hle_ptr(L.shadow) + (size_t)y * L.pitch, L.rowbytes);
    hle_free(hle_ptr(L.shadow));
    return (uint32_t)unlock();
}

void count_and_log(int k, const char *name, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void count_and_log(int k, const char *name, const char *fmt, ...) {
    int s = slot_of((Kind)k, name);
    if (++g_calls[k][s] > k_logged) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[d3d9] %s::%s", k_if[k].name, name);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "%s\n", g_calls[k][s] == k_logged ? " (further calls counted, not printed)" : "");
    va_end(ap);
}

void tex_LockRect() {                                   /* (this, level, D3DLOCKED_RECT *, const RECT *, flags) */
    IDirect3DTexture9 *t = self<IDirect3DTexture9>(TEXTURE, "LockRect");
    uint32_t level = A32(1);
    D3DSURFACE_DESC d = {};
    if (FAILED(t->GetLevelDesc(level, &d))) RET(D3DERR_INVALIDCALL_, 5);
    uint32_t hr = rect_lock(rect_key(A32(0), 0, level), "IDirect3DTexture9::LockRect", d, A32(2), A32(3), A32(4),
                            [&](D3DLOCKED_RECT *l, const RECT *r, DWORD f) { return t->LockRect(level, l, r, f); });
    count_and_log(TEXTURE, "LockRect", "(level %u, %ux%u format %u, flags 0x%X) -> 0x%08X", level, d.Width, d.Height,
                  (unsigned)d.Format, A32(4), hr);
    RET(hr, 5);
}
void tex_UnlockRect() {                                 /* (this, level) */
    IDirect3DTexture9 *t = self<IDirect3DTexture9>(TEXTURE, "UnlockRect");
    uint32_t level = A32(1);
    RET(rect_unlock(rect_key(A32(0), 0, level), [&] { return t->UnlockRect(level); }), 2);
}
void surf_LockRect() {                                  /* (this, D3DLOCKED_RECT *, const RECT *, flags) */
    IDirect3DSurface9 *sf = self<IDirect3DSurface9>(SURFACE, "LockRect");
    D3DSURFACE_DESC d = {};
    if (FAILED(sf->GetDesc(&d))) RET(D3DERR_INVALIDCALL_, 4);
    uint32_t hr = rect_lock(rect_key(A32(0), 0xFFFF, 0), "IDirect3DSurface9::LockRect", d, A32(1), A32(2), A32(3),
                            [&](D3DLOCKED_RECT *l, const RECT *r, DWORD f) { return sf->LockRect(l, r, f); });
    count_and_log(SURFACE, "LockRect", "(%ux%u format %u, flags 0x%X) -> 0x%08X", d.Width, d.Height, (unsigned)d.Format,
                  A32(3), hr);
    RET(hr, 4);
}
void surf_UnlockRect() {                                /* (this) */
    IDirect3DSurface9 *sf = self<IDirect3DSurface9>(SURFACE, "UnlockRect");
    RET(rect_unlock(rect_key(A32(0), 0xFFFF, 0), [&] { return sf->UnlockRect(); }), 1);
}

/* TM_D3D9_SHOTS=<dir>: the back buffer before Present number n is written
 * as <dir>/frame_<n>.ppm for n = 1, 2, 4, 8... and every 1000th
 * (diagnostic; through GetRenderTargetData, so the frame as the device holds it). */
void shot(IDirect3DDevice9 *d, int n) {
    static const char *dir = getenv("TM_D3D9_SHOTS");
    if (!dir || !(!(n & (n - 1)) || n % 1000 == 0)) return;
    IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC desc = {};
    bb->GetDesc(&desc);
    D3DLOCKED_RECT lr = {};
    if (SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
        SUCCEEDED(d->GetRenderTargetData(bb, sys)) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        char path[1024];
        snprintf(path, sizeof path, "%s/frame_%06d.ppm", dir, n);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", desc.Width, desc.Height);
            std::vector<uint8_t> row(desc.Width * 3);
            for (UINT y = 0; y < desc.Height; y++) {
                const uint8_t *src = (const uint8_t *)lr.pBits + (size_t)lr.Pitch * y;
                for (UINT x = 0; x < desc.Width; x++) {
                    uint32_t r, g, b;
                    if (desc.Format == D3DFMT_X1R5G5B5 || desc.Format == D3DFMT_A1R5G5B5 || desc.Format == D3DFMT_R5G6B5) {
                        uint16_t px; memcpy(&px, src + 2 * x, 2);
                        if (desc.Format == D3DFMT_R5G6B5) r = px >> 11 << 3, g = (px >> 5 & 63) << 2, b = (px & 31) << 3;
                        else r = (px >> 10 & 31) << 3, g = (px >> 5 & 31) << 3, b = (px & 31) << 3;
                    } else {
                        uint32_t px; memcpy(&px, src + 4 * x, 4);
                        r = px >> 16 & 255, g = px >> 8 & 255, b = px & 255;
                    }
                    row[3 * x] = (uint8_t)r, row[3 * x + 1] = (uint8_t)g, row[3 * x + 2] = (uint8_t)b;
                }
                fwrite(row.data(), 1, row.size(), f);
            }
            fclose(f);
        }
        sys->UnlockRect();
    }
    if (sys) sys->Release();
    bb->Release();
}

/* Present(source rect, dest rect, window override, dirty region): the window
 * override must be NULL or the device's own window (one SDL window). */
void dev_Present() {
    IDirect3DDevice9 *d = self<IDirect3DDevice9>(DEVICE, "Present");
    uint32_t src = A32(1), dst = A32(2), hwnd = A32(3), rgn = A32(4);
    if (hwnd) hle_fatal("[d3d9] IDirect3DDevice9::Present to window 0x%08X: a window override is not mapped", hwnd);
    if (rgn) hle_fatal("[d3d9] IDirect3DDevice9::Present with a dirty region is not handled");
    shot(d, g_calls[DEVICE][slot_of(DEVICE, "Present")] + 1);
    HRESULT hr = d->Present(src ? (const RECT *)hle_ptr(src) : nullptr, dst ? (const RECT *)hle_ptr(dst) : nullptr,
                            nullptr, nullptr);
    /* where the picture is, so win32hle maps the mouse onto it (the swap chain fills the window) */
    D3DPRESENT_PARAMETERS pp = {};
    IDirect3DSwapChain9 *sc = nullptr;
    if (SUCCEEDED(d->GetSwapChain(0, &sc)) && sc) {
        sc->GetPresentParameters(&pp);
        sc->Release();
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(hle_screen_sdl_window(), &w, &h);
        hle_screen_gl_picture((int)pp.BackBufferWidth, (int)pp.BackBufferHeight, 0, 0, w, h);
    }
    int n = ++g_calls[DEVICE][slot_of(DEVICE, "Present")];
    if (n <= k_logged || !(n & (n - 1)))
        fprintf(stderr, "[d3d9] IDirect3DDevice9::Present #%d -> 0x%08X\n", n, (unsigned)hr);
    RET((uint32_t)hr, 5);
}

/* a texture's level: forwarded, and its guest object remembers its texture */
void tex_GetSurfaceLevel() {                            /* (this, level, IDirect3DSurface9 **) */
    uint32_t tex = A32(0), out = A32(2);
    Fwd<&IDirect3DTexture9::GetSurfaceLevel, TEXTURE, slot_of(TEXTURE, "GetSurfaceLevel")>::call();
    uint32_t lv = out ? (uint32_t)MEM32(out) : 0;
    if (lv) g_px[lv].parent = tex;
}
void cube_GetCubeMapSurface() {                         /* (this, face, level, IDirect3DSurface9 **) */
    uint32_t tex = A32(0), out = A32(3);
    Fwd<&IDirect3DCubeTexture9::GetCubeMapSurface, CUBETEXTURE, slot_of(CUBETEXTURE, "GetCubeMapSurface")>::call();
    uint32_t lv = out ? (uint32_t)MEM32(out) : 0;
    if (lv) g_px[lv].parent = tex;
}

/* the implemented methods beyond IUnknown */
struct Impl { Kind k; const char *name; recomp_func_t fn; };
const Impl k_impl[] = {
    { DEVICE, "GetDeviceCaps", dev_GetDeviceCaps },
    FWD(DEVICE, IDirect3DDevice9, GetRenderTarget),
    FWD(DEVICE, IDirect3DDevice9, GetDepthStencilSurface),
    FWD(DEVICE, IDirect3DDevice9, GetAvailableTextureMem),
    { DEVICE, "GetDirect3D", dev_GetDirect3D },
    FWD(DEVICE, IDirect3DDevice9, GetDisplayMode),
    FWD(DEVICE, IDirect3DDevice9, CreateVertexBuffer),
    FWD(DEVICE, IDirect3DDevice9, CreateIndexBuffer),
    { VERTEXBUFFER, "Lock", buffer_Lock<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC, VERTEXBUFFER> },
    { VERTEXBUFFER, "Unlock", buffer_Unlock<IDirect3DVertexBuffer9, VERTEXBUFFER> },
    FWD(VERTEXBUFFER, IDirect3DVertexBuffer9, GetDesc),
    { INDEXBUFFER, "Lock", buffer_Lock<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC, INDEXBUFFER> },
    { INDEXBUFFER, "Unlock", buffer_Unlock<IDirect3DIndexBuffer9, INDEXBUFFER> },
    FWD(INDEXBUFFER, IDirect3DIndexBuffer9, GetDesc),
    FWD(DEVICE, IDirect3DDevice9, CreateTexture),
    FWD(TEXTURE, IDirect3DTexture9, GetLevelDesc),
    { TEXTURE, "GetSurfaceLevel", tex_GetSurfaceLevel },
    FWD(TEXTURE, IDirect3DTexture9, GetLevelCount),
    { TEXTURE, "LockRect", tex_LockRect },
    { TEXTURE, "UnlockRect", tex_UnlockRect },
    FWD(SURFACE, IDirect3DSurface9, GetDesc),
    { SURFACE, "LockRect", surf_LockRect },
    { SURFACE, "UnlockRect", surf_UnlockRect },
    { DEVICE, "Present", dev_Present },
    /* forwarded as they are: every argument converts by its type (Conv) */
    FWD(SWAPCHAIN, IDirect3DSwapChain9, GetDevice),
    FWD(SURFACE, IDirect3DSurface9, GetDevice),
    FWD(VOLUME, IDirect3DVolume9, GetDevice),
    FWD(TEXTURE, IDirect3DTexture9, GetDevice),
    FWD(CUBETEXTURE, IDirect3DCubeTexture9, GetDevice),
    FWD(VOLUMETEXTURE, IDirect3DVolumeTexture9, GetDevice),
    FWD(VERTEXBUFFER, IDirect3DVertexBuffer9, GetDevice),
    FWD(INDEXBUFFER, IDirect3DIndexBuffer9, GetDevice),
    FWD(VERTEXDECL, IDirect3DVertexDeclaration9, GetDevice),
    FWD(VERTEXSHADER, IDirect3DVertexShader9, GetDevice),
    FWD(PIXELSHADER, IDirect3DPixelShader9, GetDevice),
    FWD(STATEBLOCK, IDirect3DStateBlock9, GetDevice),
    FWD(QUERY, IDirect3DQuery9, GetDevice),
    FWD(DEVICE, IDirect3DDevice9, TestCooperativeLevel),
    FWD(DEVICE, IDirect3DDevice9, EvictManagedResources),
    FWD(DEVICE, IDirect3DDevice9, SetCursorProperties),
    FWD(DEVICE, IDirect3DDevice9, SetCursorPosition),
    FWD(DEVICE, IDirect3DDevice9, ShowCursor),
    FWD(DEVICE, IDirect3DDevice9, GetSwapChain),
    FWD(DEVICE, IDirect3DDevice9, GetNumberOfSwapChains),
    FWD(DEVICE, IDirect3DDevice9, GetBackBuffer),
    FWD(DEVICE, IDirect3DDevice9, GetRasterStatus),
    FWD(DEVICE, IDirect3DDevice9, SetDialogBoxMode),
    FWD(DEVICE, IDirect3DDevice9, SetGammaRamp),
    FWD(DEVICE, IDirect3DDevice9, GetGammaRamp),
    FWD(DEVICE, IDirect3DDevice9, CreateVolumeTexture),
    FWD(DEVICE, IDirect3DDevice9, CreateCubeTexture),
    FWD(DEVICE, IDirect3DDevice9, CreateRenderTarget),
    FWD(DEVICE, IDirect3DDevice9, CreateDepthStencilSurface),
    FWD(DEVICE, IDirect3DDevice9, UpdateSurface),
    FWD(DEVICE, IDirect3DDevice9, UpdateTexture),
    FWD(DEVICE, IDirect3DDevice9, GetRenderTargetData),
    FWD(DEVICE, IDirect3DDevice9, GetFrontBufferData),
    FWD(DEVICE, IDirect3DDevice9, StretchRect),
    FWD(DEVICE, IDirect3DDevice9, ColorFill),
    FWD(DEVICE, IDirect3DDevice9, CreateOffscreenPlainSurface),
    FWD(DEVICE, IDirect3DDevice9, SetRenderTarget),
    FWD(DEVICE, IDirect3DDevice9, SetDepthStencilSurface),
    FWD(DEVICE, IDirect3DDevice9, BeginScene),
    FWD(DEVICE, IDirect3DDevice9, EndScene),
    FWD(DEVICE, IDirect3DDevice9, Clear),
    FWD(DEVICE, IDirect3DDevice9, SetTransform),
    FWD(DEVICE, IDirect3DDevice9, GetTransform),
    FWD(DEVICE, IDirect3DDevice9, MultiplyTransform),
    FWD(DEVICE, IDirect3DDevice9, SetViewport),
    FWD(DEVICE, IDirect3DDevice9, GetViewport),
    FWD(DEVICE, IDirect3DDevice9, SetMaterial),
    FWD(DEVICE, IDirect3DDevice9, GetMaterial),
    FWD(DEVICE, IDirect3DDevice9, SetLight),
    FWD(DEVICE, IDirect3DDevice9, GetLight),
    FWD(DEVICE, IDirect3DDevice9, LightEnable),
    FWD(DEVICE, IDirect3DDevice9, GetLightEnable),
    FWD(DEVICE, IDirect3DDevice9, SetClipPlane),
    FWD(DEVICE, IDirect3DDevice9, GetClipPlane),
    FWD(DEVICE, IDirect3DDevice9, SetRenderState),
    FWD(DEVICE, IDirect3DDevice9, GetRenderState),
    FWD(DEVICE, IDirect3DDevice9, CreateStateBlock),
    FWD(DEVICE, IDirect3DDevice9, BeginStateBlock),
    FWD(DEVICE, IDirect3DDevice9, EndStateBlock),
    FWD(DEVICE, IDirect3DDevice9, SetClipStatus),
    FWD(DEVICE, IDirect3DDevice9, GetClipStatus),
    FWD(DEVICE, IDirect3DDevice9, GetTexture),
    FWD(DEVICE, IDirect3DDevice9, SetTexture),
    FWD(DEVICE, IDirect3DDevice9, GetTextureStageState),
    FWD(DEVICE, IDirect3DDevice9, SetTextureStageState),
    FWD(DEVICE, IDirect3DDevice9, GetSamplerState),
    FWD(DEVICE, IDirect3DDevice9, SetSamplerState),
    FWD(DEVICE, IDirect3DDevice9, ValidateDevice),
    FWD(DEVICE, IDirect3DDevice9, SetPaletteEntries),
    FWD(DEVICE, IDirect3DDevice9, GetPaletteEntries),
    FWD(DEVICE, IDirect3DDevice9, SetCurrentTexturePalette),
    FWD(DEVICE, IDirect3DDevice9, GetCurrentTexturePalette),
    FWD(DEVICE, IDirect3DDevice9, SetScissorRect),
    FWD(DEVICE, IDirect3DDevice9, GetScissorRect),
    FWD(DEVICE, IDirect3DDevice9, SetSoftwareVertexProcessing),
    FWD(DEVICE, IDirect3DDevice9, GetSoftwareVertexProcessing),
    FWD(DEVICE, IDirect3DDevice9, SetNPatchMode),
    FWD(DEVICE, IDirect3DDevice9, DrawPrimitive),
    FWD(DEVICE, IDirect3DDevice9, DrawIndexedPrimitive),
    FWD(DEVICE, IDirect3DDevice9, DrawPrimitiveUP),
    FWD(DEVICE, IDirect3DDevice9, DrawIndexedPrimitiveUP),
    FWD(DEVICE, IDirect3DDevice9, CreateVertexDeclaration),
    FWD(DEVICE, IDirect3DDevice9, SetVertexDeclaration),
    FWD(DEVICE, IDirect3DDevice9, GetVertexDeclaration),
    FWD(DEVICE, IDirect3DDevice9, SetFVF),
    FWD(DEVICE, IDirect3DDevice9, GetFVF),
    FWD(DEVICE, IDirect3DDevice9, CreateVertexShader),
    FWD(DEVICE, IDirect3DDevice9, SetVertexShader),
    FWD(DEVICE, IDirect3DDevice9, GetVertexShader),
    FWD(DEVICE, IDirect3DDevice9, SetVertexShaderConstantF),
    FWD(DEVICE, IDirect3DDevice9, GetVertexShaderConstantF),
    FWD(DEVICE, IDirect3DDevice9, SetVertexShaderConstantI),
    FWD(DEVICE, IDirect3DDevice9, GetVertexShaderConstantI),
    FWD(DEVICE, IDirect3DDevice9, SetVertexShaderConstantB),
    FWD(DEVICE, IDirect3DDevice9, GetVertexShaderConstantB),
    FWD(DEVICE, IDirect3DDevice9, SetStreamSource),
    FWD(DEVICE, IDirect3DDevice9, GetStreamSource),
    FWD(DEVICE, IDirect3DDevice9, SetStreamSourceFreq),
    FWD(DEVICE, IDirect3DDevice9, GetStreamSourceFreq),
    FWD(DEVICE, IDirect3DDevice9, SetIndices),
    FWD(DEVICE, IDirect3DDevice9, GetIndices),
    FWD(DEVICE, IDirect3DDevice9, CreatePixelShader),
    FWD(DEVICE, IDirect3DDevice9, SetPixelShader),
    FWD(DEVICE, IDirect3DDevice9, GetPixelShader),
    FWD(DEVICE, IDirect3DDevice9, SetPixelShaderConstantF),
    FWD(DEVICE, IDirect3DDevice9, GetPixelShaderConstantF),
    FWD(DEVICE, IDirect3DDevice9, SetPixelShaderConstantI),
    FWD(DEVICE, IDirect3DDevice9, GetPixelShaderConstantI),
    FWD(DEVICE, IDirect3DDevice9, SetPixelShaderConstantB),
    FWD(DEVICE, IDirect3DDevice9, GetPixelShaderConstantB),
    FWD(DEVICE, IDirect3DDevice9, CreateQuery),
    FWD(TEXTURE, IDirect3DTexture9, AddDirtyRect),
    FWD(TEXTURE, IDirect3DTexture9, SetLOD),
    FWD(TEXTURE, IDirect3DTexture9, GetLOD),
    FWD(TEXTURE, IDirect3DTexture9, PreLoad),
    FWD(TEXTURE, IDirect3DTexture9, GetType),
    FWD(TEXTURE, IDirect3DTexture9, SetPriority),
    FWD(TEXTURE, IDirect3DTexture9, GetPriority),
    FWD(TEXTURE, IDirect3DTexture9, SetAutoGenFilterType),
    FWD(TEXTURE, IDirect3DTexture9, GetAutoGenFilterType),
    FWD(TEXTURE, IDirect3DTexture9, GenerateMipSubLevels),
    FWD(CUBETEXTURE, IDirect3DCubeTexture9, GetLevelDesc),
    { CUBETEXTURE, "GetCubeMapSurface", cube_GetCubeMapSurface },
    FWD(CUBETEXTURE, IDirect3DCubeTexture9, GetLevelCount),
    FWD(CUBETEXTURE, IDirect3DCubeTexture9, AddDirtyRect),
    FWD(CUBETEXTURE, IDirect3DCubeTexture9, PreLoad),
    FWD(CUBETEXTURE, IDirect3DCubeTexture9, GetType),
    FWD(SURFACE, IDirect3DSurface9, GetType),
    FWD(SURFACE, IDirect3DSurface9, PreLoad),
    FWD(VERTEXBUFFER, IDirect3DVertexBuffer9, PreLoad),
    FWD(VERTEXBUFFER, IDirect3DVertexBuffer9, GetType),
    FWD(INDEXBUFFER, IDirect3DIndexBuffer9, PreLoad),
    FWD(INDEXBUFFER, IDirect3DIndexBuffer9, GetType),
    FWD(STATEBLOCK, IDirect3DStateBlock9, Capture),
    FWD(STATEBLOCK, IDirect3DStateBlock9, Apply),
    FWD(QUERY, IDirect3DQuery9, GetType),
    FWD(QUERY, IDirect3DQuery9, GetDataSize),
    FWD(QUERY, IDirect3DQuery9, Issue),
    FWD(QUERY, IDirect3DQuery9, GetData),
    FWD(VERTEXDECL, IDirect3DVertexDeclaration9, GetDeclaration),
    FWD(VERTEXSHADER, IDirect3DVertexShader9, GetFunction),
    FWD(PIXELSHADER, IDirect3DPixelShader9, GetFunction),
};

std::vector<std::string> g_names[NKIND];
std::vector<win32hle_shim> g_shims;

uint32_t vtable(Kind k) {
    if (g_vt[k]) return g_vt[k];
    uint32_t vt = hle_va(hle_alloc(4u * (uint32_t)k_if[k].n));
    for (int s = 0; s < k_if[k].n; s++) {
        uint32_t va = hle_resolve(g_names[k][(size_t)s].c_str());
        if (!va) hle_fatal("[d3d9] %s: not registered", g_names[k][(size_t)s].c_str());
        MEM32(vt + 4u * (uint32_t)s) = va;
    }
    return g_vt[k] = vt;
}

/* ---- IDirect3D9: what describes the backend, answered by DXVK ---- */
IDirect3D9 *host_d3d(uint32_t d3d) {
    auto it = g_d3d.find(d3d);
    if (it != g_d3d.end()) return it->second;
    IDirect3D9 *h = Direct3DCreate9(D3D_SDK_VERSION);
    if (!h) return nullptr;
    D3DADAPTER_IDENTIFIER9 id = {};
    if (SUCCEEDED(h->GetAdapterIdentifier(0, 0, &id)))
        fprintf(stderr, "[d3d9] DXVK: adapter 0 is \"%s\" (%s)\n", id.Description, id.Driver);
    g_d3d[d3d] = h;
    return h;
}
IDirect3D9 *d3d_self(const char *method) {
    uint32_t va = A32(0);
    if (!tm_d3d9_is_d3d(va)) hle_fatal("[d3d9] IDirect3D9::%s on 0x%08X: not a live IDirect3D9", method, va);
    IDirect3D9 *h = host_d3d(va);
    if (!h) hle_fatal("[d3d9] IDirect3D9::%s: DXVK's Direct3DCreate9 failed (no Vulkan device it accepts)", method);
    return h;
}
void d3d_CheckDeviceFormat() {               /* (this, adapter, type, adapter format, usage, resource type, format) */
    IDirect3D9 *h = d3d_self("CheckDeviceFormat");
    HRESULT hr = h->CheckDeviceFormat(A32(1), (D3DDEVTYPE)A32(2), (D3DFORMAT)A32(3), A32(4), (D3DRESOURCETYPE)A32(5),
                                      (D3DFORMAT)A32(6));
    fprintf(stderr, "[d3d9] IDirect3D9::CheckDeviceFormat(%u, type %u, adapter format %u, usage 0x%X, resource %u, format %u) -> 0x%08X\n",
            A32(1), A32(2), A32(3), A32(4), A32(5), A32(6), (unsigned)hr);
    RET((uint32_t)hr, 7);
}
void d3d_CheckDeviceType() {                 /* (this, adapter, type, display format, back buffer format, windowed) */
    IDirect3D9 *h = d3d_self("CheckDeviceType");
    HRESULT hr = h->CheckDeviceType(A32(1), (D3DDEVTYPE)A32(2), (D3DFORMAT)A32(3), (D3DFORMAT)A32(4), (BOOL)A32(5));
    fprintf(stderr, "[d3d9] IDirect3D9::CheckDeviceType(%u, type %u, display %u, back buffer %u, windowed %u) -> 0x%08X\n",
            A32(1), A32(2), A32(3), A32(4), A32(5), (unsigned)hr);
    RET((uint32_t)hr, 6);
}
void d3d_CheckDepthStencilMatch() {          /* (this, adapter, type, adapter format, render target format, depth format) */
    IDirect3D9 *h = d3d_self("CheckDepthStencilMatch");
    HRESULT hr = h->CheckDepthStencilMatch(A32(1), (D3DDEVTYPE)A32(2), (D3DFORMAT)A32(3), (D3DFORMAT)A32(4), (D3DFORMAT)A32(5));
    fprintf(stderr, "[d3d9] IDirect3D9::CheckDepthStencilMatch(%u, type %u, adapter %u, target %u, depth %u) -> 0x%08X\n",
            A32(1), A32(2), A32(3), A32(4), A32(5), (unsigned)hr);
    RET((uint32_t)hr, 6);
}
void d3d_CheckDeviceMultiSampleType() {      /* (this, adapter, type, format, windowed, multisample type, DWORD *quality) */
    IDirect3D9 *h = d3d_self("CheckDeviceMultiSampleType");
    DWORD q = 0;
    uint32_t pq = A32(6);
    HRESULT hr = h->CheckDeviceMultiSampleType(A32(1), (D3DDEVTYPE)A32(2), (D3DFORMAT)A32(3), (BOOL)A32(4),
                                               (D3DMULTISAMPLE_TYPE)A32(5), pq ? &q : nullptr);
    if (pq && SUCCEEDED(hr)) MEM32(pq) = q;
    fprintf(stderr, "[d3d9] IDirect3D9::CheckDeviceMultiSampleType(%u, type %u, format %u, windowed %u, samples %u) -> 0x%08X, quality levels %u\n",
            A32(1), A32(2), A32(3), A32(4), A32(5), (unsigned)hr, (unsigned)q);
    RET((uint32_t)hr, 7);
}
void d3d_GetDeviceCaps() {                   /* (this, adapter, type, D3DCAPS9 *) */
    IDirect3D9 *h = d3d_self("GetDeviceCaps");
    uint32_t out = A32(3);
    if (!out) RET(D3DERR_INVALIDCALL_, 4);
    D3DCAPS9 c = {};
    HRESULT hr = h->GetDeviceCaps(A32(1), (D3DDEVTYPE)A32(2), &c);
    if (SUCCEEDED(hr)) memcpy(hle_ptr(out), &c, sizeof c);
    fprintf(stderr, "[d3d9] IDirect3D9::GetDeviceCaps(%u, type %u) -> 0x%08X\n", A32(1), A32(2), (unsigned)hr);
    RET((uint32_t)hr, 4);
}
/* D3DADAPTER_IDENTIFIER9 holds no pointer; LARGE_INTEGER is 8-aligned on x86 too */
static_assert(sizeof(D3DADAPTER_IDENTIFIER9) == 1104, "D3DADAPTER_IDENTIFIER9 layout");
void d3d_GetAdapterIdentifier() {            /* (this, adapter, flags, D3DADAPTER_IDENTIFIER9 *) */
    IDirect3D9 *h = d3d_self("GetAdapterIdentifier");
    uint32_t out = A32(3);
    if (!out) RET(D3DERR_INVALIDCALL_, 4);
    D3DADAPTER_IDENTIFIER9 id = {};
    HRESULT hr = h->GetAdapterIdentifier(A32(1), A32(2), &id);
    if (SUCCEEDED(hr)) memcpy(hle_ptr(out), &id, sizeof id);
    fprintf(stderr, "[d3d9] IDirect3D9::GetAdapterIdentifier(%u, 0x%X) -> 0x%08X, \"%s\" (%s), vendor 0x%04X device 0x%04X\n",
            A32(1), A32(2), (unsigned)hr, id.Description, id.Driver, (unsigned)id.VendorId, (unsigned)id.DeviceId);
    RET((uint32_t)hr, 4);
}
const win32hle_shim k_d3d_shims[] = {
    { "IDirect3D9::CheckDeviceFormat",          d3d_CheckDeviceFormat },
    { "IDirect3D9::CheckDeviceType",            d3d_CheckDeviceType },
    { "IDirect3D9::CheckDepthStencilMatch",     d3d_CheckDepthStencilMatch },
    { "IDirect3D9::CheckDeviceMultiSampleType", d3d_CheckDeviceMultiSampleType },
    { "IDirect3D9::GetDeviceCaps",              d3d_GetDeviceCaps },
    { "IDirect3D9::GetAdapterIdentifier",       d3d_GetAdapterIdentifier },
    { nullptr, nullptr }
};

/* ---- CreateDevice ---- */
const char *fmt_name(uint32_t f) {
    switch (f) {
        case 21: return "A8R8G8B8"; case 22: return "X8R8G8B8"; case 23: return "R5G6B5"; case 24: return "X1R5G5B5";
        case 25: return "A1R5G5B5"; case 75: return "D24S8"; case 77: return "D24X8"; case 80: return "D16";
        default: return "?";
    }
}

/* D3DPRESENT_PARAMETERS in the guest: 14 dwords, hDeviceWindow the 8th */
enum { PP_W, PP_H, PP_FMT, PP_COUNT, PP_MS, PP_MSQ, PP_SWAP, PP_HWND, PP_WINDOWED, PP_AUTODS, PP_DSFMT, PP_FLAGS,
       PP_REFRESH, PP_INTERVAL, PP_N };

uint32_t create_device(uint32_t d3d, uint32_t adapter, uint32_t type, uint32_t focus, uint32_t flags, uint32_t pp,
                       uint32_t out) {
    uint32_t g[PP_N];
    for (int k = 0; k < PP_N; k++) g[k] = MEM32(pp + 4u * (uint32_t)k);
    /* the window: the guest's device window, or its focus window, shown in win32hle's one SDL window */
    uint32_t hwnd = g[PP_HWND] ? g[PP_HWND] : focus;
    if (!hwnd) {
        fprintf(stderr, "[d3d9] CreateDevice: no device window and no focus window -> D3DERR_INVALIDCALL\n");
        return D3DERR_INVALIDCALL_;
    }
    if (g[PP_HWND] && focus && g[PP_HWND] != focus)
        hle_fatal("[d3d9] CreateDevice: device window 0x%08X differs from focus window 0x%08X; one SDL window shows one "
                  "guest window, so this is not mapped", g[PP_HWND], focus);
    if (!hle_window_class(hwnd) || hle_window_parent(hwnd))
        hle_fatal("[d3d9] CreateDevice: window 0x%08X is not a guest top-level window", hwnd);
    SDL_Window *win = hle_screen_sdl_window();
    if (!win) hle_fatal("[d3d9] CreateDevice: win32hle has no window (the DXVK backend needs one)");
    IDirect3D9 *h = host_d3d(d3d);
    if (!h) {
        fprintf(stderr, "[d3d9] CreateDevice: DXVK's Direct3DCreate9 failed (no Vulkan device it accepts) -> D3DERR_NOTAVAILABLE\n");
        g_create_failed++;
        return D3DERR_NOTAVAILABLE_;
    }

    D3DPRESENT_PARAMETERS hp = {};
    hp.BackBufferWidth = g[PP_W], hp.BackBufferHeight = g[PP_H], hp.BackBufferFormat = (D3DFORMAT)g[PP_FMT];
    hp.BackBufferCount = g[PP_COUNT], hp.MultiSampleType = (D3DMULTISAMPLE_TYPE)g[PP_MS];
    hp.MultiSampleQuality = g[PP_MSQ], hp.SwapEffect = (D3DSWAPEFFECT)g[PP_SWAP], hp.hDeviceWindow = (HWND)win;
    hp.Windowed = (BOOL)g[PP_WINDOWED], hp.EnableAutoDepthStencil = (BOOL)g[PP_AUTODS];
    hp.AutoDepthStencilFormat = (D3DFORMAT)g[PP_DSFMT], hp.Flags = g[PP_FLAGS];
    hp.FullScreen_RefreshRateInHz = g[PP_REFRESH], hp.PresentationInterval = g[PP_INTERVAL];
    if (!(flags & D3DCREATE_FPU_PRESERVE))
        fprintf(stderr, "[d3d9] CreateDevice: no D3DCREATE_FPU_PRESERVE; Windows would set the calling thread's x87 "
                        "precision to 24 bits, the guest's x87 state is left as it is (docs/DXVK-INTEGRATION.md 4)\n");

    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = h->CreateDevice(adapter, (D3DDEVTYPE)type, (HWND)win, flags, &hp, &dev);
    /* Direct3D 9 writes the parameters it used back (a 0 size or count filled in) */
    uint32_t back[PP_N] = { hp.BackBufferWidth, hp.BackBufferHeight, (uint32_t)hp.BackBufferFormat, hp.BackBufferCount,
                            (uint32_t)hp.MultiSampleType, hp.MultiSampleQuality, (uint32_t)hp.SwapEffect, g[PP_HWND],
                            (uint32_t)hp.Windowed, (uint32_t)hp.EnableAutoDepthStencil, (uint32_t)hp.AutoDepthStencilFormat,
                            hp.Flags, hp.FullScreen_RefreshRateInHz, hp.PresentationInterval };
    for (int k = 0; k < PP_N; k++)
        if (back[k] != g[k]) {
            fprintf(stderr, "[d3d9] CreateDevice: parameter %d written back: %u -> %u\n", k, g[k], back[k]);
            MEM32(pp + 4u * (uint32_t)k) = back[k];
        }
    if (FAILED(hr) || !dev) {
        fprintf(stderr, "[d3d9] CreateDevice(%ux%u %s, %s, %s depth) on DXVK -> 0x%08X\n", g[PP_W], g[PP_H],
                fmt_name(g[PP_FMT]), g[PP_WINDOWED] ? "windowed" : "full screen", fmt_name(g[PP_DSFMT]), (unsigned)hr);
        g_create_failed++;
        return FAILED(hr) ? (uint32_t)hr : D3DERR_NOTAVAILABLE_;
    }
    if (!g[PP_WINDOWED]) {
        /* as on Windows, a full-screen device sets the display's mode: the guest's
         * screen size (GetSystemMetrics) and its full-screen windows follow */
        hle_set_screen_size((int)hp.BackBufferWidth, (int)hp.BackBufferHeight);
        fprintf(stderr, "[d3d9] CreateDevice: full screen, the guest's display is now %ux%u\n", hp.BackBufferWidth,
                hp.BackBufferHeight);
    }
    uint32_t va = wrap(dev, DEVICE);
    g_px[va].parent = d3d;
    MEM32(out) = va;
    fprintf(stderr, "[d3d9] CreateDevice(%ux%u %s, %s, %s depth, flags 0x%X) on DXVK -> IDirect3DDevice9 0x%08X, window 0x%08X\n",
            g[PP_W], g[PP_H], fmt_name(g[PP_FMT]), g[PP_WINDOWED] ? "windowed" : "full screen", fmt_name(g[PP_DSFMT]),
            flags, va, hwnd);
    return (uint32_t)hr;
}

void d3d_destroyed(uint32_t d3d) {
    auto it = g_d3d.find(d3d);
    if (it == g_d3d.end()) return;
    it->second->Release();                               /* a device keeps DXVK's IDirect3D9 alive on its own */
    g_d3d.erase(it);
}

void report(FILE *f) {
    for (int k = 0; k < NKIND; k++)
        if (g_made[k]) fprintf(f, "[d3d9] %s on DXVK: %d made, %d freed\n", k_if[k].name, g_made[k], g_freed[k]);
    if (g_create_failed) fprintf(f, "[d3d9] CreateDevice on DXVK failed %d times\n", g_create_failed);
    for (int k = 0; k < NKIND; k++)
        for (int m = 0; m < k_if[k].n; m++)
            if (g_calls[k][m]) fprintf(f, "[d3d9] calls %s::%s %d\n", k_if[k].name, k_if[k].m[m].name, g_calls[k][m]);
}

const tm_d3d9_backend k_backend = { "DXVK Native", create_device, d3d_destroyed, report };
}  // namespace

extern "C" const tm_d3d9_backend *tm_d3d9_dxvk_backend(void) {
    static bool done;
    if (done) return &k_backend;
    done = true;
    /* DXVK Native picks no window system by itself */
    const char *wsi = getenv("DXVK_WSI_DRIVER");
    if (!wsi) setenv("DXVK_WSI_DRIVER", "SDL2", 1);
    else if (strcmp(wsi, "SDL2")) hle_fatal("[d3d9] DXVK_WSI_DRIVER=%s: the host's window is SDL2", wsi);

    static const auto stops = all_stops(std::make_integer_sequence<int, NKIND>{});
    for (int k = 0; k < NKIND; k++)
        for (int s = 0; s < k_if[k].n; s++) g_names[k].push_back(std::string(k_if[k].name) + "::" + k_if[k].m[s].name);
    for (const Impl &m : k_impl) {                       /* a name in k_impl that is no method is a mistake */
        bool found = false;
        for (int s = 0; s < k_if[m.k].n; s++) found |= !strcmp(m.name, k_if[m.k].m[s].name);
        if (!found) hle_fatal("[d3d9] %s::%s: no such method", k_if[m.k].name, m.name);
    }
    for (int k = 0; k < NKIND; k++)
        for (int s = 0; s < k_if[k].n; s++) {
            recomp_func_t fn = stops[(size_t)k][(size_t)s];
            for (const Impl &m : k_impl)
                if (m.k == k && !strcmp(m.name, k_if[k].m[s].name)) fn = m.fn;
            g_shims.push_back(win32hle_shim{ g_names[k][(size_t)s].c_str(), fn });
        }
    g_shims.push_back(win32hle_shim{ nullptr, nullptr });
    win32hle_register(g_shims.data());
    win32hle_register(k_d3d_shims);
    return &k_backend;
}

extern "C" int tm_d3d9_dxvk_live(int kind) { return kind >= 0 && kind < NKIND ? g_made[kind] - g_freed[kind] : -1; }
