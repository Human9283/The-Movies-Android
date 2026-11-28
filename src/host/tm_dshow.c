/*
 * tm_dshow.c - the DirectShow graph objects used by the start-up probe and
 * the game's video-renderer attachment path (docs/DIRECTSHOW-STARTUP.md).
 *
 * sub_009AD7A0 creates a filter graph, a capture graph builder and the
 * Windows Media ASF Writer filter, gives the graph to the builder, adds the
 * writer to the graph and releases all three. The graph also exposes interfaces for event notification, media control,
 * position, basic audio and basic video. A separate WMAsfReader module now
 * exposes IFileSourceFilter, one RGB24 output pin, pin/media-type enumerators
 * and an FFmpeg-backed frame-delivery worker. This video-only path has not yet
 * been built or validated against the game on Android; it does not imply
 * playable media or a working main menu.
 * The startup-probe objects are implemented with real COM ownership rules:
 *
 *   CLSID_FilterGraph          IUnknown, IFilterGraph, IGraphBuilder
 *     AddFilter: keeps a reference to the filter and its name, and calls
 *     its IBaseFilter::JoinFilterGraph(graph, name). The filter does not
 *     reference the graph back. NULL names receive generated four-digit
 *     names; duplicate names are suffixed and return VFW_S_DUPLICATE_NAME.
 *     Adding the same filter twice returns E_FAIL. The last Release removes every filter
 *     (JoinFilterGraph(NULL, NULL), then Release) and frees the graph.
 *   CLSID_CaptureGraphBuilder2 IUnknown, ICaptureGraphBuilder2
 *     SetFiltergraph: keeps a reference to the graph (E_UNEXPECTED if one
 *     is set). The last Release releases it.
 *   CLSID_WMAsfWriter          IUnknown, IPersist, IMediaFilter, IBaseFilter
 *     GetClassID, GetState (always State_Stopped), JoinFilterGraph,
 *     QueryFilterInfo (the name and an AddRef'd graph).
 *
 * The ASF writer here is NOT a working filter. It has no pins, no profile,
 * no IConfigAsfWriter or IFileSinkFilter, and it never leaves the stopped
 * state: it can be created, joined to a graph, queried and destroyed, which
 * is all the probe does. Writing a .wmv (film export) is a separate
 * capability, not implemented (docs/IMPORTS.md, task C).
 *
 * Unimplemented methods remain self-naming stubs in their vtables. QueryInterface
 * for a supported interface returns a distinct interface pointer with the
 * controlling object's IUnknown identity. Other supported-by-Windows but not
 * yet implemented interfaces fail explicitly.
 *
 * COM: each exposed interface has a guest-memory pointer whose first
 * dword is its interface-specific vtable of shim VAs. The graph's primary
 * IGraphBuilder pointer is the controlling IUnknown; IMediaEventSink,
 * IMediaControl, IMediaPosition, IBasicAudio and IBasicVideo use separate
 * interface pointers but share the graph's reference count and return the
 * primary pointer for IID_IUnknown.
 * Host-side state is resolved from the interface pointer. Filters in the
 * graph may be guest objects: their methods are called through
 * hle_call_guest. Vtables are made at the first CoCreateInstance, not at
 * start-up, so the guest heap before that point is unchanged.
 *
 * Threads: the WMAsfReader owns one decoder worker. It allocates its guest
 * thread ID before pthread_create, adopts it in the worker, serializes guest
 * memory access with mach_enter/mach_leave, and releases the machine lock
 * while joining. The game's hidden IMediaEventSink::Notify entry is routed
 * through the graph's AddFilter vtable slot as the game expects.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "win32hle.h"
#include "tm_dshow.h"
#include "tm_dshow_reader.h"

#define S_OK                   0x00000000u
#define E_NOINTERFACE          0x80004002u
#define E_POINTER              0x80004003u
#define E_INVALIDARG            0x80070057u
#define E_FAIL                 0x80004005u
#define E_NOTIMPL               0x80004001u
#define VFW_E_CANNOT_RENDER     0x80040218u
#define VFW_E_NOT_STOPPED       0x80040224u
#define VFW_E_NOT_CONNECTED     0x80040209u
#define VFW_S_STATE_INTERMEDIATE 0x00040237u
#define VFW_S_CANT_CUE          0x00040268u
#define VFW_E_ENUM_OUT_OF_SYNC  0x80040203u
#define VFW_E_NOT_FOUND         0x80040216u
#define VFW_S_DUPLICATE_NAME    0x0004022Du
#define VFW_E_DUPLICATE_NAME    0x8004022Du
#define E_UNEXPECTED           0x8000FFFFu
#define E_OUTOFMEMORY          0x8007000Eu
#define CLASS_E_NOAGGREGATION  0x80040110u
#define MAX_FILTER_NAME        128           /* WCHARs, the terminator included */
#define FILTER_INFO_SIZE       (MAX_FILTER_NAME * 2 + 4)

typedef uint8_t guid_t[16];
#define G(d1, d2, d3, ...) { (d1) & 0xFF, ((d1) >> 8) & 0xFF, ((d1) >> 16) & 0xFF, ((d1) >> 24) & 0xFF, \
                             (d2) & 0xFF, (d2) >> 8, (d3) & 0xFF, (d3) >> 8, __VA_ARGS__ }
static const guid_t CLSID_FilterGraph_          = G(0xE436EBB3, 0x524F, 0x11CE, 0x9F,0x53,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t CLSID_CaptureGraphBuilder2_ = G(0xBF87B6E1, 0x8C27, 0x11D0, 0xB3,0xF0,0x00,0xAA,0x00,0x37,0x61,0xC5);
static const guid_t CLSID_WMAsfWriter_          = G(0x7C23220E, 0x55BB, 0x11D3, 0x8B,0x16,0x00,0xC0,0x4F,0xB6,0xBD,0x3D);
static const guid_t CLSID_WMAsfReader_          = G(0x187463A0, 0x5BB7, 0x11D3, 0xAC,0xBE,0x00,0x80,0xC7,0x5E,0x24,0x6E);

static const guid_t IID_IUnknown_        = G(0x00000000, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);
static const guid_t IID_IPersist_        = G(0x0000010C, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);
static const guid_t IID_IReferenceClock_  = G(0x56A86897, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IMediaFilter_    = G(0x56A86899, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IBaseFilter_     = G(0x56A86895, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IFilterGraph_    = G(0x56A8689F, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IGraphBuilder_   = G(0x56A868A9, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IMediaEventSink_ = G(0x56A868A2, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IMediaControl_   = G(0x56A868B1, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IMediaPosition_  = G(0x56A868B2, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IBasicAudio_     = G(0x56A868B3, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IBasicVideo_     = G(0x56A868B5, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_ICaptureGraphBuilder2_ = G(0x93E5A4E0, 0x2D50, 0x11D2, 0xAB,0xFA,0x00,0xA0,0xC9,0xC6,0xE3,0x8D);
static const guid_t IID_IEnumFilters_ = G(0x56A86893, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);

/* Interfaces the real objects have and these do not: asking for one stops
 * the run, naming it (all of these GUIDs are in Movies.exe). */
typedef struct { const char *name; guid_t iid; } named_iid;
static const named_iid g_graph_missing[] = {
    { "IFilterGraph2",  G(0x36B73882, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IMediaEvent",    G(0x56A868B6, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaEventEx",  G(0x56A868C0, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaSeeking",  G(0x36B73880, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IVideoWindow",   G(0x56A868B4, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { NULL, {0} }
};
static const named_iid g_writer_missing[] = {
    { "IConfigAsfWriter",   G(0x45086030, 0xF7E4, 0x486A, 0xB5,0x04,0x82,0x6B,0xB5,0x79,0x2A,0x3B) },
    { "IConfigAsfWriter2",  G(0x7989CCAA, 0x53F0, 0x44F0, 0x88,0x4A,0xF3,0xB0,0x3F,0x6A,0xE0,0x66) },
    { "IFileSinkFilter",    G(0xA2104830, 0x7C70, 0x11CF, 0x8B,0xCE,0x00,0xAA,0x00,0xA3,0xF1,0xA6) },
    { "IFileSinkFilter2",   G(0x00855B90, 0xCE1B, 0x11D0, 0xBD,0x4F,0x00,0xA0,0xC9,0x11,0xCE,0x86) },
    { "IMediaSeeking",      G(0x36B73880, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IAMFilterMiscFlags", G(0x2DD74950, 0xA890, 0x11D1, 0xAB,0xE8,0x00,0xA0,0xC9,0x05,0xF3,0x75) },
    { NULL, {0} }
};

/* ---- objects ---- */
enum { OBJ_GRAPH = 1, OBJ_BUILDER, OBJ_WRITER };
static const char *const g_kind_name[] = { "?", "FilterGraph", "CaptureGraphBuilder2", "WMAsfWriter" };
#define MAX_FILTERS 32
typedef struct {
    uint32_t va;                          /* [va] = vtable; 0: free slot */
    int      kind, refs;
    /* graph: its filters, each referenced, with its name (guest WCHARs) */
    int      nf;
    int      asf_readers;                  /* video sources whose Stop is a per-frame tick */
    uint32_t name_index;                   /* DirectShow's generated-name sequence, 0000..9999 */
    uint32_t graph_version;                /* invalidates IEnumFilters snapshots on mutation */
    uint32_t filter[MAX_FILTERS], fname[MAX_FILTERS];
    uint32_t removing_filter[MAX_FILTERS];
    uint32_t removing_count;
    int tearing_down, teardown_failed;
    /* IMediaEventSink is a distinct COM interface pointer with its own vtable. */
    uint32_t event_sink_va, last_event_code, last_event_param1, last_event_param2;
    uint32_t media_control_va, media_position_va, basic_audio_va, basic_video_va;
    uint32_t media_filter_va, sync_source_va;
    uint32_t event_count;
    uint32_t filter_state, media_loaded;
    double media_duration, media_position, media_stop_time, media_preroll_time;
    double media_rate, avg_time_per_frame;
    int32_t media_width, media_height;
    int32_t audio_volume, audio_balance;
    /* builder: the graph it was given (referenced) */
    uint32_t graph;
    /* writer: the graph it joined (not referenced) and the name it was given */
    uint32_t joined;
    uint32_t sync_source_va;
    uint16_t name[MAX_FILTER_NAME];
} obj_t;
#define MAX_OBJS 64
static obj_t g_obj[MAX_OBJS];
static uint32_t graph_render_file(obj_t *graph, uint32_t file_name);
static int g_created[4], g_destroyed[4];
static uint32_t g_vt[4];
static uint32_t g_vt_event_sink, g_vt_media_control, g_vt_media_filter;
static uint32_t g_vt_media_position, g_vt_basic_audio, g_vt_basic_video;
static uint32_t g_vt_enum_filters;

#define MAX_ENUM_FILTERS 64
typedef struct {
    uint32_t va;
    int refs;
    uint32_t graph;
    uint32_t version;
    uint32_t count, cursor;
    uint32_t filters[MAX_FILTERS];
} enum_filters_t;
static enum_filters_t g_enum_filters[MAX_ENUM_FILTERS];

static obj_t *obj_of(uint32_t va, int kind) {
    for (int i = 0; i < MAX_OBJS; i++) if (va && g_obj[i].va == va && g_obj[i].kind == kind) return &g_obj[i];
    return NULL;
}
/* `this` of a method: an object of this kind that is alive, or the run stops. */
static obj_t *self(int kind, const char *method) {
    obj_t *o = obj_of(A32(0), kind);
    if (!o) hle_fatal("[dshow] %s::%s on 0x%08X: not a live %s object", g_kind_name[kind], method, A32(0), g_kind_name[kind]);
    return o;
}
static void make_vtables(void);
static obj_t *obj_new(int kind) {
    make_vtables();
    for (int i = 0; i < MAX_OBJS; i++)
        if (!g_obj[i].va) {
            uint8_t *p = hle_alloc(16);
            if (!p) return NULL;
            memset(&g_obj[i], 0, sizeof g_obj[i]);
            g_obj[i].va = hle_va(p), g_obj[i].kind = kind, g_obj[i].refs = 1;
            if (kind == OBJ_GRAPH) g_obj[i].media_rate = 1.0;
            MEM32(g_obj[i].va) = g_vt[kind];
            g_created[kind]++;
            return &g_obj[i];
        }
    return NULL;
}
static void obj_free(obj_t *o) {
    fprintf(stderr, "[dshow] %s 0x%08X destroyed\n", g_kind_name[o->kind], o->va);
    g_destroyed[o->kind]++;
    hle_free(hle_ptr(o->va));
    memset(o, 0, sizeof *o);
}

/* A method of any COM object, through its vtable (it may be guest code). */
static uint32_t vcall(uint32_t obj, int slot, int nargs, const uint32_t *args) {
    uint32_t a[4] = { obj };
    for (int i = 0; i < nargs; i++) a[1 + i] = args[i];
    return hle_call_guest(MEM32(MEM32(obj) + 4u * (uint32_t)slot), nargs + 1, a);
}
static uint32_t com_addref(uint32_t obj)  { return vcall(obj, 1, 0, NULL); }
static uint32_t com_release(uint32_t obj) { return vcall(obj, 2, 0, NULL); }

static void wname(const uint16_t *w, char *out, size_t n) {       /* for the log: ASCII, '?' for the rest */
    size_t k = 0;
    for (; w && w[k] && k + 1 < n; k++) out[k] = w[k] >= 0x20 && w[k] < 0x7F ? (char)w[k] : '?';
    out[k] = 0;
}
static size_t wlen(const uint16_t *w) { size_t k = 0; while (w[k]) k++; return k; }

/* The interfaces each kind answers for, and those of the real object it
 * lacks (asking for one stops the run). */
static const guid_t *const *g_ok[4];
static const named_iid *g_missing[4];

/* Whether the object has the interface iid (host bytes): S_OK, or
 * E_NOINTERFACE; an interface of the real object that is missing here
 * stops the run. No reference is taken. */
static uint32_t has_iface(const obj_t *o, const uint8_t *iid) {
    for (int k = 0; g_ok[o->kind][k]; k++)
        if (!memcmp(iid, *g_ok[o->kind][k], 16)) return S_OK;
    char g[40];
    hle_guid_string(iid, g);
    for (const named_iid *m = g_missing[o->kind]; m->name; m++)
        if (!memcmp(iid, m->iid, 16))
            hle_fatal("[dshow] %s::QueryInterface(%s = %s): the real object has this interface; not implemented here",
                      g_kind_name[o->kind], g, m->name);
    fprintf(stderr, "[dshow] %s::QueryInterface(%s) -> E_NOINTERFACE\n", g_kind_name[o->kind], g);
    return E_NOINTERFACE;
}
/* QueryInterface(this, riid, ppv), for every kind. */
static uint32_t query(obj_t *o, uint32_t riid, uint32_t ppv) {
    if (!ppv) return E_POINTER;
    MEM32(ppv) = 0;
    if (!riid) return E_POINTER;
    uint32_t hr = has_iface(o, (const uint8_t *)hle_ptr(riid));
    if (!hr) o->refs++, MEM32(ppv) = o->va;
    return hr;
}

/* ---- the filter graph: IGraphBuilder (IFilterGraph, IUnknown) ---- */
static const guid_t *const g_graph_ok[] = { &IID_IUnknown_, &IID_IPersist_, &IID_IMediaFilter_, &IID_IFilterGraph_, &IID_IGraphBuilder_, NULL };

static uint32_t graph_event_sink(obj_t *o) {
    if (o->event_sink_va) return o->event_sink_va;
    uint32_t *p = (uint32_t *)hle_alloc(16);
    if (!p) return 0;
    p[0] = g_vt_event_sink;
    o->event_sink_va = hle_va(p);
    return o->event_sink_va;
}

static uint32_t graph_secondary_interface(obj_t *o, uint32_t *field, uint32_t vtable) {
    if (*field) return *field;
    uint32_t *p = (uint32_t *)hle_alloc(4);
    if (!p) return 0;
    p[0] = vtable;
    *field = hle_va(p);
    return *field;
}
static uint32_t graph_media_control(obj_t *o) {
    return graph_secondary_interface(o, &o->media_control_va, g_vt_media_control);
}
static uint32_t graph_media_filter(obj_t *o) {
    return graph_secondary_interface(o, &o->media_filter_va, g_vt_media_filter);
}
static uint32_t graph_media_position(obj_t *o) {
    return graph_secondary_interface(o, &o->media_position_va, g_vt_media_position);
}
static uint32_t graph_basic_audio(obj_t *o) {
    return graph_secondary_interface(o, &o->basic_audio_va, g_vt_basic_audio);
}
static uint32_t graph_basic_video(obj_t *o) {
    return graph_secondary_interface(o, &o->basic_video_va, g_vt_basic_video);
}

static uint32_t graph_query(obj_t *o, uint32_t riid, uint32_t ppv) {
    if (!ppv) return E_POINTER;
    MEM32(ppv) = 0;
    if (!riid) return E_POINTER;
    if (!memcmp(hle_ptr(riid), IID_IMediaEventSink_, 16)) {
        uint32_t sink = graph_event_sink(o);
        if (!sink) return E_OUTOFMEMORY;
        o->refs++;
        MEM32(ppv) = sink;
        return S_OK;
    }
    const uint8_t *iid = (const uint8_t *)hle_ptr(riid);
    uint32_t interface_va = 0;
    if (!memcmp(iid, IID_IMediaFilter_, 16) || !memcmp(iid, IID_IPersist_, 16))
        interface_va = graph_media_filter(o);
    else if (!memcmp(iid, IID_IMediaControl_, 16))
        interface_va = graph_media_control(o);
    else if (!memcmp(iid, IID_IMediaPosition_, 16))
        interface_va = graph_media_position(o);
    else if (!memcmp(iid, IID_IBasicAudio_, 16))
        interface_va = graph_basic_audio(o);
    else if (!memcmp(iid, IID_IBasicVideo_, 16))
        interface_va = graph_basic_video(o);
    if (interface_va) {
        o->refs++;
        MEM32(ppv) = interface_va;
        return S_OK;
    }
    if (!memcmp(iid, IID_IMediaFilter_, 16) || !memcmp(iid, IID_IPersist_, 16) ||
        !memcmp(iid, IID_IMediaControl_, 16) ||
        !memcmp(iid, IID_IMediaPosition_, 16) ||
        !memcmp(iid, IID_IBasicAudio_, 16) ||
        !memcmp(iid, IID_IBasicVideo_, 16))
        return E_OUTOFMEMORY;
    return query(o, riid, ppv);
}

static obj_t *event_sink_owner(uint32_t sink_va) {
    for (int i = 0; sink_va && i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_GRAPH && g_obj[i].event_sink_va == sink_va)
            return &g_obj[i];
    hle_fatal("[dshow] IMediaEventSink on 0x%08X: no live FilterGraph owns this interface", sink_va);
    return NULL;
}

static obj_t *media_filter_owner(uint32_t filter_va) {
    for (int i = 0; filter_va && i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_GRAPH && g_obj[i].media_filter_va == filter_va)
            return &g_obj[i];
    hle_fatal("[dshow] IMediaFilter on 0x%08X: no live FilterGraph owns this interface", filter_va);
    return NULL;
}

static obj_t *media_control_owner(uint32_t control_va) {
    for (int i = 0; control_va && i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_GRAPH && g_obj[i].media_control_va == control_va)
            return &g_obj[i];
    hle_fatal("[dshow] IMediaControl on 0x%08X: no live FilterGraph owns this interface", control_va);
    return NULL;
}
static obj_t *media_position_owner(uint32_t va) {
    for (int i = 0; va && i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_GRAPH && g_obj[i].media_position_va == va)
            return &g_obj[i];
    hle_fatal("[dshow] IMediaPosition on 0x%08X: no live FilterGraph owns this interface", va);
    return NULL;
}
static obj_t *basic_audio_owner(uint32_t va) {
    for (int i = 0; va && i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_GRAPH && g_obj[i].basic_audio_va == va)
            return &g_obj[i];
    hle_fatal("[dshow] IBasicAudio on 0x%08X: no live FilterGraph owns this interface", va);
    return NULL;
}
static obj_t *basic_video_owner(uint32_t va) {
    for (int i = 0; va && i < MAX_OBJS; i++)
        if (g_obj[i].va && g_obj[i].kind == OBJ_GRAPH && g_obj[i].basic_video_va == va)
            return &g_obj[i];
    hle_fatal("[dshow] IBasicVideo on 0x%08X: no live FilterGraph owns this interface", va);
    return NULL;
}

/* The secondary interface Release methods share the graph's controlling
 * reference count; its implementation is below the interface vtables. */
static uint32_t graph_release(uint32_t va);
static uint32_t graph_disconnect_filter_pins(uint32_t filter);

static void media_position_QueryInterface(void) {
    obj_t *o = media_position_owner(A32(0));
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void media_position_AddRef(void) {
    obj_t *o = media_position_owner(A32(0));
    RET((uint32_t)++o->refs, 1);
}
static void media_position_Release(void) {
    obj_t *o = media_position_owner(A32(0));
    RET(graph_release(o->va), 1);
}
static void basic_audio_QueryInterface(void) {
    obj_t *o = basic_audio_owner(A32(0));
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void basic_audio_AddRef(void) {
    obj_t *o = basic_audio_owner(A32(0));
    RET((uint32_t)++o->refs, 1);
}
static void basic_audio_Release(void) {
    obj_t *o = basic_audio_owner(A32(0));
    RET(graph_release(o->va), 1);
}
static void basic_video_QueryInterface(void) {
    obj_t *o = basic_video_owner(A32(0));
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void basic_video_AddRef(void) {
    obj_t *o = basic_video_owner(A32(0));
    RET((uint32_t)++o->refs, 1);
}
static void basic_video_Release(void) {
    obj_t *o = basic_video_owner(A32(0));
    RET(graph_release(o->va), 1);
}

static void graph_QueryInterface(void) {                /* (this, riid, ppv) */
    obj_t *o = self(OBJ_GRAPH, "QueryInterface");
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void graph_AddRef(void) { obj_t *o = self(OBJ_GRAPH, "AddRef"); RET((uint32_t)++o->refs, 1); }

/* Release is shared by IGraphBuilder and the graph's secondary interfaces.
 * Every interface reference contributes to the same controlling IUnknown. */
static uint32_t graph_release(uint32_t va) {
    obj_t *o = obj_of(va, OBJ_GRAPH);
    if (!o) hle_fatal("[dshow] Release on 0x%08X: not a live FilterGraph", va);
    if (o->tearing_down) {
        if (o->refs > 1) o->refs--;
        return (uint32_t)o->refs;
    }
    int n = --o->refs;
    if (n < 0) hle_fatal("[dshow] FilterGraph 0x%08X: reference count underflow", va);
    if (n == 0) o->refs = 1;
    if (n == 0 || (n == 1 && o->teardown_failed)) {
        uint32_t filters[MAX_FILTERS], names[MAX_FILTERS];
        int count = o->nf;
        memcpy(filters, o->filter, (size_t)count * sizeof(filters[0]));
        memcpy(names, o->fname, (size_t)count * sizeof(names[0]));
        o->tearing_down = 1;
        o->teardown_failed = 0;
        for (int i = 0; i < count; i++) com_addref(filters[i]);
        uint32_t failure = S_OK;
        for (int i = count - 1; i >= 0; i--) {
            uint32_t hr = vcall(filters[i], 4, 0, NULL);
            if (hr & 0x80000000u) { failure = hr; break; }
        }
        for (int i = count - 1; !(failure & 0x80000000u) && i >= 0; i--) {
            uint32_t hr = graph_disconnect_filter_pins(filters[i]);
            if (hr & 0x80000000u) { failure = hr; break; }
            uint32_t none[2] = { 0, 0 };
            hr = vcall(filters[i], 13, 2, none);
            if (hr & 0x80000000u) { failure = hr; break; }
        }
        if (failure & 0x80000000u) {
            for (int i = 0; i < count; i++) com_release(filters[i]);
            o = obj_of(va, OBJ_GRAPH);
            if (o) {
                o->tearing_down = 0;
                o->teardown_failed = 1;
                if (o->refs < 1) o->refs = 1;
            }
            fprintf(stderr, "[dshow] FilterGraph 0x%08X: final teardown deferred -> 0x%08X\n", va, failure);
            return o ? (uint32_t)o->refs : 0;
        }
        o = obj_of(va, OBJ_GRAPH);
        if (!o) hle_fatal("[dshow] FilterGraph 0x%08X vanished during final teardown", va);
        o->nf = 0;
        o->asf_readers = 0;
        o->graph_version++;
        for (int i = 0; i < count; i++) {
            o->filter[i] = 0;
            o->fname[i] = 0;
            if (names[i]) hle_free(hle_ptr(names[i]));
            com_release(filters[i]);
            com_release(filters[i]);
        }
        o = obj_of(va, OBJ_GRAPH);
        if (!o) hle_fatal("[dshow] FilterGraph 0x%08X vanished while releasing filters", va);
        if (o->refs > 1) {
            o->tearing_down = 0;
            o->teardown_failed = 0;
            o->refs--;
            return (uint32_t)o->refs;
        }
        /* Release the graph clock before freeing secondary interfaces:
         * Release is external COM code and may re-enter QueryInterface or
         * retain a new graph reference. Re-check the controlling count after
         * that callback before destroying any interface storage. */
        uint32_t clock = o->sync_source_va;
        o->sync_source_va = 0;
        if (clock) com_release(clock);
        o = obj_of(va, OBJ_GRAPH);
        if (!o) hle_fatal("[dshow] FilterGraph 0x%08X vanished while releasing its clock", va);
        if (o->refs > 1) {
            o->tearing_down = 0;
            o->teardown_failed = 0;
            o->refs--;
            return (uint32_t)o->refs;
        }
        if (o->event_sink_va) hle_free(hle_ptr(o->event_sink_va));
        if (o->media_control_va) hle_free(hle_ptr(o->media_control_va));
        if (o->media_position_va) hle_free(hle_ptr(o->media_position_va));
        if (o->basic_audio_va) hle_free(hle_ptr(o->basic_audio_va));
        if (o->basic_video_va) hle_free(hle_ptr(o->basic_video_va));
        if (o->media_filter_va) hle_free(hle_ptr(o->media_filter_va));
        o->refs = 0;
        obj_free(o);
        return 0;
    }
    return (uint32_t)n;
}
static void graph_Release(void) { RET(graph_release(A32(0)), 1); }

/* IMediaEventSink has its own IUnknown vtable. Returning the graph pointer
 * for this IID would make slot 3 dispatch to IGraphBuilder::AddFilter. */
static void event_sink_QueryInterface(void) {
    obj_t *o = event_sink_owner(A32(0));
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void event_sink_AddRef(void) {
    obj_t *o = event_sink_owner(A32(0));
    RET((uint32_t)++o->refs, 1);
}
static void event_sink_Release(void) {
    obj_t *o = event_sink_owner(A32(0));
    RET(graph_release(o->va), 1);
}
static void event_sink_Notify(void) {                   /* (this, event, param1, param2) */
    obj_t *o = event_sink_owner(A32(0));
    uint32_t event_code = A32(1), param1 = A32(2), param2 = A32(3);
    o->last_event_code = event_code;
    o->last_event_param1 = param1;
    o->last_event_param2 = param2;
    o->event_count++;
    if (o->event_count <= 16 || !(o->event_count & (o->event_count - 1)))
        fprintf(stderr, "[dshow] IMediaEventSink::Notify graph=0x%08X event=0x%08X p1=0x%08X p2=0x%08X count=%u\n",
                o->va, event_code, param1, param2, o->event_count);
    RET(S_OK, 4);
}

/* IMediaControl is IDispatch-based (IUnknown + IDispatch + nine methods).
 * It has a separate guest vtable; its IUnknown identity remains the graph. */
static void media_control_QueryInterface(void) {
    obj_t *o = media_control_owner(A32(0));
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void media_control_AddRef(void) {
    obj_t *o = media_control_owner(A32(0));
    RET((uint32_t)++o->refs, 1);
}
static void media_control_Release(void) {
    obj_t *o = media_control_owner(A32(0));
    RET(graph_release(o->va), 1);
}

static uint32_t graph_filter_transition(obj_t *o, int method, int new_state,
                                            uint32_t start_low, uint32_t start_high) {
    uint32_t graph_va = o->va;
    o->refs++;                                  /* hold graph across child callbacks */
    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof filters[0]);

    /* Keep every filter alive for the full transition. A guest callback can
     * re-enter the graph and remove a filter while the state change is in
     * progress. */
    for (int i = 0; i < count; i++) com_addref(filters[i]);

    uint32_t first_failure = S_OK;
    int incomplete = 0;

    /* DirectShow changes graph state downstream-first for Run, Pause and
     * Stop. Renderers must be ready before a source delivers, and stopping a
     * renderer first releases a source thread blocked in Receive. The native
     * game graph adds its source before its renderer, so walk backwards. */
    int i = count - 1;
    int step = -1;
    for (; i >= 0 && i < count; i += step) {
        uint32_t args[2] = { start_low, start_high };
        uint32_t hr = vcall(filters[i], method, method == 6 ? 2 : 0, method == 6 ? args : NULL);
        if ((hr & 0x80000000u) && first_failure == S_OK)
            first_failure = hr;
        else if (hr == 1u)                               /* S_FALSE: asynchronous state change */
            incomplete = 1;
    }

    for (int k = 0; k < count; k++) com_release(filters[k]);

    o = obj_of(graph_va, OBJ_GRAPH);
    if (!o) {
        graph_release(graph_va);
        return E_UNEXPECTED;
    }
    if (!(first_failure & 0x80000000u))
        o->filter_state = (uint32_t)new_state;
    uint32_t result = (first_failure & 0x80000000u) ? first_failure
                    : incomplete ? 1u /* S_FALSE */ : S_OK;
    graph_release(graph_va);
    return result;
}

static uint32_t graph_run_at(obj_t *o, uint64_t start_time) {
    uint32_t graph_va = o->va;
    o->refs++;
    if (o->filter_state == 2) {
        graph_release(graph_va);
        return S_OK;
    }
    uint32_t pause_hr = graph_filter_transition(o, 5, 1, 0, 0);
    if (pause_hr & 0x80000000u) {
        graph_release(graph_va);
        return pause_hr;
    }
    o = obj_of(graph_va, OBJ_GRAPH);
    if (!o) {
        graph_release(graph_va);
        return E_UNEXPECTED;
    }
    uint32_t run_hr = graph_filter_transition(o, 6, 2,
                                              (uint32_t)start_time,
                                              (uint32_t)(start_time >> 32));
    uint32_t result = (run_hr & 0x80000000u) ? run_hr : pause_hr;
    graph_release(graph_va);
    return result;
}
static uint64_t graph_reference_time(obj_t *o) {
    if (o->sync_source_va) {
        uint64_t *time_mem = (uint64_t *)hle_alloc(sizeof(uint64_t));
        if (time_mem) {
            uint32_t clock = o->sync_source_va;
            com_addref(clock);
            uint32_t args[1] = { hle_va(time_mem) };
            uint32_t hr = vcall(clock, 3, 1, args);      /* IReferenceClock::GetTime */
            uint64_t value = *time_mem;
            com_release(clock);
            hle_free(time_mem);
            if (hr == S_OK || hr == 1u /* S_FALSE */) return value;
        }
    }
    return hle_mono_ns() / 100u;
}

static uint32_t graph_run(obj_t *o) {
    uint32_t graph_va = o->va;
    o->refs++;
    uint64_t start = graph_reference_time(o) + 1000000u;
    uint32_t hr = graph_run_at(o, start);
    graph_release(graph_va);
    return hr;
}
static uint32_t graph_pause(obj_t *o) {
    return graph_filter_transition(o, 5, 1, 0, 0);
}
static uint32_t graph_stop(obj_t *o) {
    if (o->asf_readers > 0) {
        o->filter_state = 0;
        return S_OK;
    }
    return graph_filter_transition(o, 4, 0, 0, 0);
}

static uint32_t graph_get_state(obj_t *o, uint32_t timeout_ms, uint32_t state_out) {               /* (this, timeout, FILTER_STATE *) */
    if (!state_out) return E_POINTER;
    MEM32(state_out) = o->filter_state;

    uint32_t graph_va = o->va;
    o->refs++;
    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof filters[0]);
    for (int i = 0; i < count; i++) com_addref(filters[i]);

    const uint64_t started_ns = hle_mono_ns();
    const uint64_t deadline_ns = timeout_ms == 0xFFFFFFFFu
        ? UINT64_MAX
        : started_ns + (uint64_t)timeout_ms * 1000000ull;
    uint32_t result = S_OK;

    /* Ask each child filter for its actual state. A graph can report Paused
     * while a renderer is still waiting for its first sample; in that case
     * GetState must report the intermediate transition rather than claiming
     * the video has already been cued. */
    for (int i = count - 1; i >= 0; i--) {
        uint32_t *child_state = (uint32_t *)hle_alloc(4);
        if (!child_state) {
            result = E_OUTOFMEMORY;
            break;
        }
        MEM32(hle_va(child_state)) = 0xFFFFFFFFu;

        uint32_t remaining_ms = 0xFFFFFFFFu;
        if (deadline_ns != UINT64_MAX) {
            uint64_t now = hle_mono_ns();
            uint64_t remaining_ns = now < deadline_ns ? deadline_ns - now : 0;
            uint64_t rounded_ms = (remaining_ns + 999999ull) / 1000000ull;
            remaining_ms = rounded_ms > 0xFFFFFFFEu ? 0xFFFFFFFEu : (uint32_t)rounded_ms;
        }

        /* Even a zero timeout is a nonblocking poll: ask every child whether
         * its transition is already complete, rather than declaring the graph
         * intermediate before inspecting any filter. */
        uint32_t args[2] = { remaining_ms, hle_va(child_state) };
        uint32_t hr = vcall(filters[i], 7, 2, args);      /* IMediaFilter::GetState */
        hle_free(child_state);

        if (hr & 0x80000000u) {
            result = hr;
            break;
        }
        if (hr == VFW_S_CANT_CUE) {
            result = VFW_S_CANT_CUE;
            break;
        }
        if (hr == VFW_S_STATE_INTERMEDIATE)
            result = VFW_S_STATE_INTERMEDIATE;
    }

    for (int i = 0; i < count; i++) com_release(filters[i]);
    if (!obj_of(graph_va, OBJ_GRAPH)) result = E_UNEXPECTED;
    graph_release(graph_va);
    return result;
}

static void media_control_Run(void) {
    obj_t *o = media_control_owner(A32(0));
    RET(graph_run(o), 1);
}
static void media_control_Pause(void) {
    obj_t *o = media_control_owner(A32(0));
    RET(graph_pause(o), 1);
}
static void media_control_Stop(void) {
    obj_t *o = media_control_owner(A32(0));
    RET(graph_stop(o), 1);
}
static void media_control_GetState(void) {
    obj_t *o = media_control_owner(A32(0));
    RET(graph_get_state(o, A32(1), A32(2)), 3);
}
static void media_filter_GetState(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET(graph_get_state(o, A32(1), A32(2)), 3);
}

static void media_filter_QueryInterface(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET(graph_query(o, A32(1), A32(2)), 3);
}
static void media_filter_AddRef(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET((uint32_t)++o->refs, 1);
}
static void media_filter_Release(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET(graph_release(o->va), 1);
}
static void media_filter_GetClassID(void) {
    media_filter_owner(A32(0));
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    memcpy(hle_ptr(out), CLSID_FilterGraph_, 16);
    RET(S_OK, 2);
}
static void media_filter_Stop(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET(graph_stop(o), 1);
}
static void media_filter_Pause(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET(graph_pause(o), 1);
}
static void media_filter_Run(void) {
    obj_t *o = media_filter_owner(A32(0));
    uint64_t start = (uint64_t)A32(1) | ((uint64_t)A32(2) << 32);
    RET(graph_run_at(o, start), 3);
}

/* Propagate a selected graph clock to every child. If one filter rejects the
 * clock, restore the previous reference on all children before returning. */
static uint32_t graph_set_sync_source(obj_t *o, uint32_t clock) {
    if (o->filter_state != 0) return VFW_E_NOT_STOPPED;
    uint32_t graph_va = o->va;
    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof(filters[0]));

    o->refs++;
    if (clock) com_addref(clock);
    for (int i = 0; i < count; i++) com_addref(filters[i]);

    uint32_t previous = o->sync_source_va;
    uint32_t result = S_OK;
    for (int i = count - 1; i >= 0; i--) {
        uint32_t args[1] = { clock };
        uint32_t hr = vcall(filters[i], 8, 1, args);
        if (hr & 0x80000000u) { result = hr; break; }
    }

    if (result & 0x80000000u) {
        for (int i = count - 1; i >= 0; i--) {
            uint32_t args[1] = { previous };
            uint32_t hr = vcall(filters[i], 8, 1, args);
            if (hr & 0x80000000u)
                fprintf(stderr, "[dshow] rollback SetSyncSource filter=0x%08X -> 0x%08X\n",
                        filters[i], hr);
        }
        if (clock) com_release(clock);
    } else {
        o = obj_of(graph_va, OBJ_GRAPH);
        if (!o) {
            result = E_UNEXPECTED;
            if (clock) com_release(clock);
        } else {
            o->sync_source_va = clock;
            if (previous) com_release(previous);
        }
    }

    for (int i = 0; i < count; i++) com_release(filters[i]);
    graph_release(graph_va);
    return result;
}

static void media_filter_SetSyncSource(void) {
    obj_t *o = media_filter_owner(A32(0));
    RET(graph_set_sync_source(o, A32(1)), 2);
}
static void media_filter_GetSyncSource(void) {
    obj_t *o = media_filter_owner(A32(0));
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = o->sync_source_va;
    if (o->sync_source_va) com_addref(o->sync_source_va);
    RET(S_OK, 2);
}

/* Select a renderer clock when one is exposed. With no clock-providing
 * filter, the ASF reader retains its monotonic frame pacing. */
static void graph_SetDefaultSyncSource(void) {
    obj_t *o = self(OBJ_GRAPH, "SetDefaultSyncSource");
    if (o->filter_state != 0) RET(VFW_E_NOT_STOPPED, 1);
    uint32_t graph_va = o->va;
    o->refs++;
    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof(filters[0]));
    for (int i = 0; i < count; i++) com_addref(filters[i]);

    uint8_t *iid = (uint8_t *)hle_alloc(16);
    uint32_t *out = (uint32_t *)hle_alloc(4);
    if (!iid || !out) {
        if (iid) hle_free(iid);
        if (out) hle_free(out);
        for (int i = 0; i < count; i++) com_release(filters[i]);
        graph_release(graph_va);
        RET(E_OUTOFMEMORY, 1);
    }
    memcpy(iid, IID_IReferenceClock_, 16);
    uint32_t iid_va = hle_va(iid), out_va = hle_va(out);
    uint32_t selected = 0;
    for (int i = count - 1; i >= 0; i--) {
        MEM32(out_va) = 0;
        uint32_t args[2] = { iid_va, out_va };
        uint32_t hr = vcall(filters[i], 0, 2, args);
        if (hr == S_OK && MEM32(out_va)) {
            selected = MEM32(out_va);
            break;
        }
        if (MEM32(out_va)) com_release(MEM32(out_va));
    }
    hle_free(iid);
    hle_free(out);
    for (int i = 0; i < count; i++) com_release(filters[i]);

    o = obj_of(graph_va, OBJ_GRAPH);
    if (!o) {
        if (selected) com_release(selected);
        graph_release(graph_va);
        RET(E_UNEXPECTED, 1);
    }
    uint32_t hr = graph_set_sync_source(o, selected);
    if (selected) com_release(selected);
    graph_release(graph_va);
    RET(hr, 1);
}

/* IMediaControl's automation methods have a different ABI from the similarly
 * named IGraphBuilder methods: RenderFile(BSTR) and AddSourceFilter(BSTR,
 * IDispatch **). Keep their argument counts and output-pointer semantics
 * separate so a call through the secondary interface cannot corrupt the
 * guest stack. */
static void media_control_RenderFile(void) {             /* (this, BSTR filename) */
    obj_t *g = media_control_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    RET(graph_render_file(g, A32(1)), 2);
}
static void media_control_AddSourceFilter(void) {        /* (this, BSTR filename, IDispatch **out) */
    media_control_owner(A32(0));
    uint32_t out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!A32(1)) RET(E_POINTER, 3);
    fprintf(stderr, "[dshow] IMediaControl::AddSourceFilter: no source filter is registered\n");
    RET(VFW_E_CANNOT_RENDER, 3);
}
static double media_position_argument(void) {
    union { uint64_t bits; double value; } v;
    v.bits = (uint64_t)A32(1) | ((uint64_t)A32(2) << 32);
    return v.value;
}
static void media_position_write_double(uint32_t out, double value) {
    if (out) memcpy(hle_ptr(out), &value, sizeof value);
}
static void media_position_get_Duration(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded) RET(E_NOTIMPL, 2);
    media_position_write_double(A32(1), o->media_duration);
    RET(S_OK, 2);
}
static void media_position_put_CurrentPosition(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!o->media_loaded) RET(E_NOTIMPL, 3);
    double value = media_position_argument();
    if (!isfinite(value) || value < 0.0) RET(E_INVALIDARG, 3);
    if (o->media_duration > 0.0 && value > o->media_duration) value = o->media_duration;
    o->media_position = value;
    RET(S_OK, 3);
}
static void media_position_get_CurrentPosition(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded) RET(E_NOTIMPL, 2);
    media_position_write_double(A32(1), o->media_position);
    RET(S_OK, 2);
}
static void media_position_put_StopTime(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!o->media_loaded) RET(E_NOTIMPL, 3);
    double value = media_position_argument();
    if (!isfinite(value) || value < 0.0) RET(E_INVALIDARG, 3);
    o->media_stop_time = value;
    RET(S_OK, 3);
}
static void media_position_get_StopTime(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded) RET(E_NOTIMPL, 2);
    media_position_write_double(A32(1), o->media_stop_time);
    RET(S_OK, 2);
}
static void media_position_put_PrerollTime(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!o->media_loaded) RET(E_NOTIMPL, 3);
    double value = media_position_argument();
    if (!isfinite(value) || value < 0.0) RET(E_INVALIDARG, 3);
    o->media_preroll_time = value;
    RET(S_OK, 3);
}
static void media_position_get_PrerollTime(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded) RET(E_NOTIMPL, 2);
    media_position_write_double(A32(1), o->media_preroll_time);
    RET(S_OK, 2);
}
static void media_position_put_Rate(void) {
    obj_t *o = media_position_owner(A32(0));
    double value = media_position_argument();
    if (!isfinite(value) || value <= 0.0) RET(E_INVALIDARG, 3);
    o->media_rate = value;
    RET(S_OK, 3);
}
static void media_position_get_Rate(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    media_position_write_double(A32(1), o->media_rate);
    RET(S_OK, 2);
}
static void media_position_CanSeekForward(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    MEM32(A32(1)) = o->media_duration > 0.0;
    RET(S_OK, 2);
}
static void media_position_CanSeekBackward(void) {
    obj_t *o = media_position_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    MEM32(A32(1)) = o->media_duration > 0.0;
    RET(S_OK, 2);
}

static void basic_audio_put_Volume(void) {
    obj_t *o = basic_audio_owner(A32(0));
    int32_t value = (int32_t)A32(1);
    if (value < -10000 || value > 0) RET(E_INVALIDARG, 2);
    o->audio_volume = value;
    RET(S_OK, 2);
}
static void basic_audio_get_Volume(void) {
    obj_t *o = basic_audio_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    MEM32(A32(1)) = (uint32_t)o->audio_volume;
    RET(S_OK, 2);
}
static void basic_audio_put_Balance(void) {
    obj_t *o = basic_audio_owner(A32(0));
    int32_t value = (int32_t)A32(1);
    if (value < -10000 || value > 10000) RET(E_INVALIDARG, 2);
    o->audio_balance = value;
    RET(S_OK, 2);
}
static void basic_audio_get_Balance(void) {
    obj_t *o = basic_audio_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    MEM32(A32(1)) = (uint32_t)o->audio_balance;
    RET(S_OK, 2);
}
static void basic_video_get_VideoWidth(void) {
    obj_t *o = basic_video_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded || o->media_width <= 0) RET(E_NOTIMPL, 2);
    MEM32(A32(1)) = (uint32_t)o->media_width;
    RET(S_OK, 2);
}
static void basic_video_get_VideoHeight(void) {
    obj_t *o = basic_video_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded || o->media_height <= 0) RET(E_NOTIMPL, 2);
    MEM32(A32(1)) = (uint32_t)o->media_height;
    RET(S_OK, 2);
}
static void basic_video_GetVideoSize(void) {
    obj_t *o = basic_video_owner(A32(0));
    uint32_t width = A32(1), height = A32(2);
    if (!width || !height) RET(E_POINTER, 3);
    if (!o->media_loaded || o->media_width <= 0 || o->media_height <= 0) RET(E_NOTIMPL, 3);
    MEM32(width) = (uint32_t)o->media_width;
    MEM32(height) = (uint32_t)o->media_height;
    RET(S_OK, 3);
}

static void basic_video_get_AvgTimePerFrame(void) {
    obj_t *o = basic_video_owner(A32(0));
    if (!A32(1)) RET(E_POINTER, 2);
    if (!o->media_loaded || o->avg_time_per_frame <= 0.0) RET(E_NOTIMPL, 2);
    media_position_write_double(A32(1), o->avg_time_per_frame);
    RET(S_OK, 2);
}

static int name_used(const obj_t *g, const uint16_t *w) {
    for (int k = 0; k < g->nf; k++) {
        const uint16_t *f = (const uint16_t *)hle_ptr(g->fname[k]);
        size_t i = 0;
        while (f[i] && f[i] == w[i]) i++;
        if (f[i] == w[i]) return 1;                      /* names compare exactly; case is not folded [doc. silent] */
    }
    return 0;
}
/* IGraphBuilder::Connect delegates media-type negotiation to the
 * output pin. A NULL AM_MEDIA_TYPE asks the pin pair to negotiate. */
static void graph_Connect(void) {                       /* (this, IPin *out, IPin *in) */
    self(OBJ_GRAPH, "Connect");
    uint32_t out_pin = A32(1), in_pin = A32(2);
    if (!out_pin || !in_pin) RET(E_POINTER, 3);
    uint32_t args[2] = { in_pin, 0 };
    RET(vcall(out_pin, 3, 2, args), 3);                  /* IPin::Connect */
}

static void graph_ConnectDirect(void) {                 /* (this, out, in, AM_MEDIA_TYPE *) */
    self(OBJ_GRAPH, "ConnectDirect");
    uint32_t out_pin = A32(1), in_pin = A32(2), media_type = A32(3);
    if (!out_pin || !in_pin) RET(E_POINTER, 4);
    uint32_t args[2] = { in_pin, media_type };
    RET(vcall(out_pin, 3, 2, args), 4);                  /* IPin::Connect */
}

static void graph_Disconnect(void) {                    /* (this, IPin *pin) */
    self(OBJ_GRAPH, "Disconnect");
    uint32_t pin = A32(1);
    if (!pin) RET(E_POINTER, 2);
    RET(vcall(pin, 5, 0, NULL), 2);                      /* IPin::Disconnect */
}

static void graph_Reconnect(void) {                     /* (this, IPin *pin) */
    self(OBJ_GRAPH, "Reconnect");
    uint32_t pin = A32(1);
    if (!pin) RET(E_POINTER, 2);

    uint32_t *peer_mem = (uint32_t *)hle_alloc(4);
    if (!peer_mem) RET(E_OUTOFMEMORY, 2);
    uint32_t peer_va = hle_va(peer_mem);
    MEM32(peer_va) = 0;
    uint32_t connected_args[1] = { peer_va };
    uint32_t hr = vcall(pin, 6, 1, connected_args);      /* IPin::ConnectedTo */
    uint32_t peer = MEM32(peer_va);
    hle_free(peer_mem);
    if (hr & 0x80000000u) RET(hr, 2);
    if (!peer) RET(VFW_E_NOT_CONNECTED, 2);

    hr = vcall(pin, 5, 0, NULL);
    if (hr & 0x80000000u) {
        vcall(peer, 2, 0, NULL);
        RET(hr, 2);
    }
    uint32_t peer_disconnect = vcall(peer, 5, 0, NULL);
    if (peer_disconnect & 0x80000000u) {
        vcall(peer, 2, 0, NULL);
        RET(peer_disconnect, 2);
    }

    uint32_t connect_args[2] = { peer, 0 };
    hr = vcall(pin, 3, 2, connect_args);                 /* IPin::Connect */
    vcall(peer, 2, 0, NULL);                             /* ConnectedTo returned an AddRef */
    RET(hr, 2);
}

/* Connect one source output pin to the first compatible input pin in the graph. */
static uint32_t graph_connect_output_pin(uint32_t graph_va, uint32_t out_pin) {
    obj_t *o = obj_of(graph_va, OBJ_GRAPH);
    if (!o || !out_pin) return E_POINTER;
    o->refs++;
    com_addref(out_pin);

    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof(filters[0]));
    for (int i = 0; i < count; i++) com_addref(filters[i]);

    uint32_t *pin_mem = (uint32_t *)hle_alloc(4);
    uint32_t *fetched_mem = (uint32_t *)hle_alloc(4);
    uint32_t *direction_mem = (uint32_t *)hle_alloc(4);
    if (!pin_mem || !fetched_mem || !direction_mem) {
        if (pin_mem) hle_free(pin_mem);
        if (fetched_mem) hle_free(fetched_mem);
        if (direction_mem) hle_free(direction_mem);
        for (int i = 0; i < count; i++) com_release(filters[i]);
        com_release(out_pin);
        graph_release(graph_va);
        return E_OUTOFMEMORY;
    }
    uint32_t pin_va = hle_va(pin_mem);
    uint32_t fetched_va = hle_va(fetched_mem);
    uint32_t direction_va = hle_va(direction_mem);
    uint32_t result = VFW_E_CANNOT_RENDER;
    int connected = 0;
    for (int i = 0; i < count && !connected; i++) {
        uint32_t enum_args[1] = { pin_va };
        MEM32(pin_va) = 0;
        uint32_t hr = vcall(filters[i], 10, 1, enum_args);
        uint32_t enumerator = MEM32(pin_va);
        if (hr & 0x80000000u) {
            if (enumerator) com_release(enumerator);
            result = hr;
            continue;
        }
        if (!enumerator) {
            result = E_UNEXPECTED;
            continue;
        }

        for (;;) {
            MEM32(pin_va) = 0;
            MEM32(fetched_va) = 0;
            uint32_t next_args[3] = { 1, pin_va, fetched_va };
            hr = vcall(enumerator, 3, 3, next_args);
            uint32_t fetched = MEM32(fetched_va);
            uint32_t pin = MEM32(pin_va);
            if (hr & 0x80000000u) {
                if (pin) com_release(pin);
                result = hr;
                break;
            }
            if (!fetched) {
                if (pin) com_release(pin);
                break;
            }
            if (!pin) {
                result = E_UNEXPECTED;
                break;
            }

            MEM32(direction_va) = 0xFFFFFFFFu;
            uint32_t direction_args[1] = { direction_va };
            uint32_t direction_hr = vcall(pin, 9, 1, direction_args);
            if (!(direction_hr & 0x80000000u) && MEM32(direction_va) == 0) {
                uint32_t connect_args[2] = { pin, 0 };
                uint32_t connect_hr = vcall(out_pin, 3, 2, connect_args);
                if (!(connect_hr & 0x80000000u)) {
                    result = S_OK;
                    connected = 1;
                } else {
                    result = connect_hr;
                }
            } else if (direction_hr & 0x80000000u) {
                result = direction_hr;
            }
            com_release(pin);
            if (connected) break;
        }
        com_release(enumerator);
    }

    hle_free(pin_mem);
    hle_free(fetched_mem);
    hle_free(direction_mem);
    for (int i = 0; i < count; i++) com_release(filters[i]);
    com_release(out_pin);
    graph_release(graph_va);
    return result;
}
static uint32_t graph_connect_reader_output(uint32_t graph_va, uint32_t filter_va) {
    uint32_t *pin_mem = (uint32_t *)hle_alloc(4);
    uint32_t *fetched_mem = (uint32_t *)hle_alloc(4);
    if (!pin_mem || !fetched_mem) {
        if (pin_mem) hle_free(pin_mem);
        if (fetched_mem) hle_free(fetched_mem);
        return E_OUTOFMEMORY;
    }
    uint32_t pin_va = hle_va(pin_mem);
    uint32_t fetched_va = hle_va(fetched_mem);
    uint32_t enum_args[1] = { pin_va };
    MEM32(pin_va) = 0;
    uint32_t hr = vcall(filter_va, 10, 1, enum_args);    /* IBaseFilter::EnumPins */
    uint32_t enumerator = MEM32(pin_va);
    if ((hr & 0x80000000u) || !enumerator) {
        if (enumerator) com_release(enumerator);
        hle_free(pin_mem);
        hle_free(fetched_mem);
        return (hr & 0x80000000u) ? hr : E_UNEXPECTED;
    }

    uint32_t result = VFW_E_CANNOT_RENDER;
    for (;;) {
        MEM32(pin_va) = 0;
        MEM32(fetched_va) = 0;
        uint32_t next_args[3] = { 1, pin_va, fetched_va };
        hr = vcall(enumerator, 3, 3, next_args);
        uint32_t pin = MEM32(pin_va);
        uint32_t fetched = MEM32(fetched_va);
        if (hr & 0x80000000u) {
            if (pin) com_release(pin);
            result = hr;
            break;
        }
        if (!fetched) {
            if (pin) com_release(pin);
            break;
        }
        if (!pin) {
            result = E_UNEXPECTED;
            break;
        }

        uint32_t *direction_mem = (uint32_t *)hle_alloc(4);
        if (!direction_mem) {
            com_release(pin);
            result = E_OUTOFMEMORY;
            break;
        }
        uint32_t direction_va = hle_va(direction_mem);
        MEM32(direction_va) = 0xFFFFFFFFu;
        uint32_t direction_args[1] = { direction_va };
        uint32_t direction_hr = vcall(pin, 9, 1, direction_args);
        uint32_t direction = MEM32(direction_va);
        hle_free(direction_mem);
        if (!(direction_hr & 0x80000000u) && direction == 1) {
            result = graph_connect_output_pin(graph_va, pin);
            com_release(pin);
            break;
        }
        com_release(pin);
        if (hr & 0x80000000u) break;
    }
    com_release(enumerator);
    hle_free(pin_mem);
    hle_free(fetched_mem);
    return result;
}

static uint32_t graph_render_file(obj_t *graph, uint32_t file_name) {
    if (!graph || !file_name) return E_POINTER;
    uint32_t graph_va = graph->va;
    graph->refs++;
#define RENDER_FILE_RET(value) do { uint32_t _render_hr = (value); graph_release(graph_va); return _render_hr; } while (0)
    uint32_t filter = 0;
    uint32_t hr = tm_dshow_reader_add_source_filter(graph_va, file_name, 0, &filter);
    if (hr & 0x80000000u) RENDER_FILE_RET(hr);

    hr = graph_connect_reader_output(graph_va, filter);
    if (hr & 0x80000000u) {
        uint32_t remove_args[1] = { filter };
        (void)vcall(graph_va, 4, 1, remove_args);
        com_release(filter);
        RENDER_FILE_RET(hr);
    }
    com_release(filter);
    RENDER_FILE_RET(S_OK);
#undef RENDER_FILE_RET
}
static void graph_RenderFile(void) {                    /* (this, file, playlist) */
    obj_t *g = self(OBJ_GRAPH, "RenderFile");
    if (!A32(1)) RET(E_POINTER, 3);
    RET(graph_render_file(g, A32(1)), 3);
}
static void graph_AddSourceFilter(void) {               /* (this, file, name, &filter) */
    obj_t *g = self(OBJ_GRAPH, "AddSourceFilter");
    uint32_t out = A32(3);
    if (!out) RET(E_POINTER, 4);
    MEM32(out) = 0;
    if (!A32(1)) RET(E_POINTER, 4);
    uint32_t filter = 0;
    uint32_t hr = tm_dshow_reader_add_source_filter(g->va, A32(1), A32(2), &filter);
    if (!(hr & 0x80000000u)) MEM32(out) = filter;
    RET(hr, 4);
}

/* IGraphBuilder::Render walks the graph's filters and tries their input pins.
 * The filters and enumerators are COM objects owned by the guest, so the host
 * does not assume a pin count, pin name, or media-type layout. */
static void graph_Render(void) {                        /* (this, IPin *out) */
    obj_t *o = self(OBJ_GRAPH, "Render");
    uint32_t out_pin = A32(1);
    if (!out_pin) RET(E_POINTER, 2);
    RET(graph_connect_output_pin(o->va, out_pin), 2);
}

static void ascii_wide(const char *src, uint16_t *dst) {
    size_t i = 0;
    while (src[i] && i < MAX_FILTER_NAME - 1) {
        dst[i] = (uint8_t)src[i];
        i++;
    }
    dst[i] = 0;
}

/* Match the Filter Graph Manager's conventional generated-name sequence:
 * "0000" for a NULL name, or "<requested name> 0000" after a collision.
 * FILTER_INFO::achName can expose at most 127 UTF-16 code units plus NUL. */
static int generated_filter_name(obj_t *g, const uint16_t *base, uint16_t *out) {
    for (unsigned tries = 0; tries < 10000; tries++) {
        unsigned serial = g->name_index;
        g->name_index = (g->name_index + 1u) % 10000u;

        char suffix[8];
        uint16_t wide_suffix[8];
        snprintf(suffix, sizeof(suffix), "%04u", serial);
        ascii_wide(suffix, wide_suffix);
        size_t suffix_len = wlen(wide_suffix);
        size_t base_len = base ? wlen(base) : 0;
        size_t separator_len = base ? 1 : 0;
        size_t keep = base_len;
        size_t available = MAX_FILTER_NAME - 1 - suffix_len - separator_len;
        if (keep > available) keep = available;

        if (base) {
            memcpy(out, base, keep * sizeof(*out));
            out[keep++] = ' ';
        }
        memcpy(out + keep, wide_suffix, (suffix_len + 1) * sizeof(*out));
        if (!name_used(g, out)) return 1;
    }
    return 0;
}

static int filter_is_asf_reader(uint32_t filter) {
    uint8_t *clsid = (uint8_t *)hle_alloc(16);
    if (!clsid) return -1;
    uint32_t clsid_va = hle_va(clsid);
    uint32_t args[1] = { clsid_va };
    uint32_t hr = vcall(filter, 3, 1, args);             /* IPersist::GetClassID */
    int is_reader = !(hr & 0x80000000u) && !memcmp(clsid, CLSID_WMAsfReader_, 16);
    hle_free(clsid);
    return is_reader;
}

static void graph_AddFilter(void) {                     /* (this, IBaseFilter *filter, LPCWSTR name) */
    obj_t *g = self(OBJ_GRAPH, "AddFilter");
    uint32_t f = A32(1), pname = A32(2), va = g->va;
    if (g->tearing_down || g->teardown_failed) RET(VFW_E_WRONG_STATE, 3);

    /* NULL is still an invalid AddFilter argument. The game's
     * TEXTURERENDERER invokes IMediaEventSink::Notify through its
     * IFilterGraph pointer; Notify(EC_COMPLETE, p1, p2) arrives in this
     * vtable slot with a small nonzero event code in place of the filter. */
    if (!f) RET(E_POINTER, 3);
    /* Preserve Notify's four-argument stdcall cleanup. */
    if (f < 0x10000u) {
        uint32_t event_code = f, param1 = pname, param2 = A32(3);
        g->last_event_code = event_code;
        g->last_event_param1 = param1;
        g->last_event_param2 = param2;
        g->event_count++;
        if (g->event_count <= 16 || !(g->event_count & (g->event_count - 1)))
            fprintf(stderr, "[dshow] FilterGraph::AddFilter-dispatch -> IMediaEventSink::Notify graph=0x%08X event=0x%08X p1=0x%08X p2=0x%08X count=%u\n",
                    va, event_code, param1, param2, g->event_count);
        RET(S_OK, 4);
    }

    if (!f) RET(E_POINTER, 3);
    for (int k = 0; k < g->nf; k++)
        if (g->filter[k] == f) RET(E_FAIL, 3);
    if (g->nf == MAX_FILTERS) RET(E_OUTOFMEMORY, 3);

    uint16_t w[MAX_FILTER_NAME] = { 0 };
    int renamed = 0;
    if (!pname) {
        if (!generated_filter_name(g, NULL, w)) RET(VFW_E_DUPLICATE_NAME, 3);
    } else {
        const uint16_t *src = (const uint16_t *)hle_ptr(pname);
        size_t n = 0;
        while (src[n] && n < MAX_FILTER_NAME - 1) {
            w[n] = src[n];
            n++;
        }
        w[n] = 0;
        if (name_used(g, w)) {
            uint16_t unique[MAX_FILTER_NAME] = { 0 };
            if (!generated_filter_name(g, w, unique)) RET(VFW_E_DUPLICATE_NAME, 3);
            memcpy(w, unique, sizeof(w));
            renamed = 1;
        }
    }

    char nm[MAX_FILTER_NAME];
    wname(w, nm, sizeof nm);
    uint16_t *keep = (uint16_t *)hle_alloc(MAX_FILTER_NAME * 2);
    if (!keep) RET(E_OUTOFMEMORY, 3);
    memcpy(keep, w, (wlen(w) + 1) * 2);
    g->refs++;
#define ADD_FILTER_RET(value) do { uint32_t _add_hr = (value); graph_release(va); RET(_add_hr, 3); } while (0)
    com_addref(f);
    int is_asf_reader = filter_is_asf_reader(f);
    if (is_asf_reader < 0) {
        com_release(f);
        hle_free(keep);
        ADD_FILTER_RET(E_OUTOFMEMORY);
    }
    uint32_t join[2] = { va, hle_va(keep) };
    uint32_t jr = vcall(f, 13, 2, join);                /* IBaseFilter::JoinFilterGraph(graph, name) */
    g = obj_of(va, OBJ_GRAPH);
    if (jr & 0x80000000u || !g) {
        com_release(f);
        hle_free(keep);
        fprintf(stderr, "[dshow] FilterGraph::AddFilter(0x%08X, \"%s\"): JoinFilterGraph -> 0x%08X\n", f, nm, jr);
        ADD_FILTER_RET(g ? jr : E_UNEXPECTED);
    }
    if (g->sync_source_va) {
        uint32_t clock_args[1] = { g->sync_source_va };
        uint32_t clock_hr = vcall(f, 8, 1, clock_args); /* IMediaFilter::SetSyncSource */
        if (clock_hr & 0x80000000u) {
            uint32_t none[2] = { 0, 0 };
            (void)vcall(f, 13, 2, none);
            com_release(f);
            hle_free(keep);
            ADD_FILTER_RET(clock_hr);
        }
    }
    g->filter[g->nf] = f, g->fname[g->nf] = hle_va(keep), g->nf++;
    if (is_asf_reader) {
        g->asf_readers++;
        double duration = 0.0, frame_seconds = 0.0;
        int width = 0, height = 0;
        if (tm_dshow_reader_media_info(f, &duration, &frame_seconds, &width, &height)) {
            g->media_loaded = 1;
            g->media_duration = duration;
            g->media_position = 0.0;
            g->media_stop_time = duration;
            g->avg_time_per_frame = frame_seconds;
            g->media_width = width;
            g->media_height = height;
        }
    }
    g->graph_version++;
    uint32_t result = renamed ? VFW_S_DUPLICATE_NAME : S_OK;
    fprintf(stderr, "[dshow] FilterGraph 0x%08X: AddFilter(0x%08X, \"%s\") -> 0x%08X, %d filter%s\n",
            va, f, nm, result, g->nf, g->nf == 1 ? "" : "s");
    ADD_FILTER_RET(result);
#undef ADD_FILTER_RET
}

/* ---- IEnumFilters and the remaining IFilterGraph collection methods ---- */
static enum_filters_t *enum_filters_of(uint32_t va) {
    for (int i = 0; i < MAX_ENUM_FILTERS; i++)
        if (va && g_enum_filters[i].va == va) return &g_enum_filters[i];
    return NULL;
}

static enum_filters_t *enum_filters_self(const char *method) {
    enum_filters_t *e = enum_filters_of(A32(0));
    if (!e)
        hle_fatal("[dshow] IEnumFilters::%s on 0x%08X: not a live enumerator", method, A32(0));
    return e;
}

static uint32_t enum_filters_refresh(enum_filters_t *e) {
    obj_t *g = obj_of(e->graph, OBJ_GRAPH);
    if (!g) return E_UNEXPECTED;
    e->count = (uint32_t)g->nf;
    e->cursor = 0;
    e->version = g->graph_version;
    if (e->count) memcpy(e->filters, g->filter, e->count * sizeof(e->filters[0]));
    if (e->count < MAX_FILTERS)
        memset(e->filters + e->count, 0, (MAX_FILTERS - e->count) * sizeof(e->filters[0]));
    return S_OK;
}

static enum_filters_t *enum_filters_new(obj_t *g, const enum_filters_t *source) {
    make_vtables();
    uint32_t *memory = (uint32_t *)hle_alloc(4);
    if (!memory) return NULL;
    enum_filters_t *e = NULL;
    for (int i = 0; i < MAX_ENUM_FILTERS; i++)
        if (!g_enum_filters[i].va) { e = &g_enum_filters[i]; break; }
    if (!e) { hle_free(memory); return NULL; }

    memset(e, 0, sizeof(*e));
    e->va = hle_va(memory);
    e->refs = 1;
    e->graph = g->va;
    g->refs++;
    *memory = g_vt_enum_filters;
    if (source) {
        e->version = source->version;
        e->count = source->count;
        e->cursor = source->cursor;
        if (e->count) memcpy(e->filters, source->filters, e->count * sizeof(e->filters[0]));
    } else if (enum_filters_refresh(e) != S_OK) {
        uint32_t va = e->va;
        memset(e, 0, sizeof(*e));
        hle_free(hle_ptr(va));
        graph_release(g->va);
        return NULL;
    }
    return e;
}

static uint32_t enum_filters_release(uint32_t va) {
    enum_filters_t *e = enum_filters_of(va);
    if (!e) hle_fatal("[dshow] IEnumFilters::Release on 0x%08X: not a live enumerator", va);
    if (e->refs <= 0) hle_fatal("[dshow] IEnumFilters 0x%08X: reference count underflow", va);
    int refs = --e->refs;
    if (!refs) {
        uint32_t graph = e->graph;
        memset(e, 0, sizeof(*e));
        hle_free(hle_ptr(va));
        graph_release(graph);
    }
    return (uint32_t)refs;
}

static void enum_filters_QueryInterface(void) {
    enum_filters_t *e = enum_filters_self("QueryInterface");
    uint32_t iid = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!iid) RET(E_POINTER, 3);
    if (memcmp(hle_ptr(iid), IID_IUnknown_, 16) &&
        memcmp(hle_ptr(iid), IID_IEnumFilters_, 16))
        RET(E_NOINTERFACE, 3);
    e->refs++;
    MEM32(out) = e->va;
    RET(S_OK, 3);
}
static void enum_filters_AddRef(void) {
    enum_filters_t *e = enum_filters_self("AddRef");
    RET((uint32_t)++e->refs, 1);
}
static void enum_filters_Release(void) {
    RET(enum_filters_release(A32(0)), 1);
}
static void enum_filters_Next(void) {
    enum_filters_t *e = enum_filters_self("Next");
    uint32_t requested = A32(1), out = A32(2), fetched_out = A32(3);
    if (requested && !out) RET(E_POINTER, 4);
    if (requested != 1 && !fetched_out) RET(E_POINTER, 4);
    if (fetched_out) MEM32(fetched_out) = 0;

    obj_t *g = obj_of(e->graph, OBJ_GRAPH);
    if (!g) RET(E_UNEXPECTED, 4);
    if (e->version != g->graph_version) RET(VFW_E_ENUM_OUT_OF_SYNC, 4);

    uint32_t available = e->cursor < e->count ? e->count - e->cursor : 0;
    uint32_t take = requested < available ? requested : available;
    for (uint32_t i = 0; i < take; i++) {
        uint32_t filter = e->filters[e->cursor++];
        com_addref(filter);
        MEM32(out + 4u * i) = filter;
    }
    if (fetched_out) MEM32(fetched_out) = take;
    RET(take == requested ? S_OK : 1u, 4);
}
static void enum_filters_Skip(void) {
    enum_filters_t *e = enum_filters_self("Skip");
    obj_t *g = obj_of(e->graph, OBJ_GRAPH);
    if (!g) RET(E_UNEXPECTED, 2);
    if (e->version != g->graph_version) RET(VFW_E_ENUM_OUT_OF_SYNC, 2);
    uint32_t requested = A32(1);
    uint32_t available = e->cursor < e->count ? e->count - e->cursor : 0;
    if (requested > available) {
        e->cursor = e->count;
        RET(1u, 2);
    }
    e->cursor += requested;
    RET(S_OK, 2);
}
static void enum_filters_Reset(void) {
    enum_filters_t *e = enum_filters_self("Reset");
    RET(enum_filters_refresh(e), 1);
}
static void enum_filters_Clone(void) {
    enum_filters_t *e = enum_filters_self("Clone");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = 0;
    obj_t *g = obj_of(e->graph, OBJ_GRAPH);
    if (!g) RET(E_UNEXPECTED, 2);
    enum_filters_t *clone = enum_filters_new(g, e);
    if (!clone) RET(E_OUTOFMEMORY, 2);
    MEM32(out) = clone->va;
    RET(S_OK, 2);
}

static uint32_t graph_disconnect_filter_pins(uint32_t filter) {
    /* The local WMAsfWriter is only a graph-lifetime probe and exposes no
       pins. Its unimplemented EnumPins slot must not be called. */
    if (obj_of(filter, OBJ_WRITER)) return S_OK;

    uint32_t *pin_mem = (uint32_t *)hle_alloc(4);
    uint32_t *fetched_mem = (uint32_t *)hle_alloc(4);
    if (!pin_mem || !fetched_mem) {
        if (pin_mem) hle_free(pin_mem);
        if (fetched_mem) hle_free(fetched_mem);
        return E_OUTOFMEMORY;
    }
    uint32_t pin_va = hle_va(pin_mem), fetched_va = hle_va(fetched_mem);
    uint32_t enum_args[1] = { pin_va };
    MEM32(pin_va) = 0;
    uint32_t hr = vcall(filter, 10, 1, enum_args);
    uint32_t enumerator = MEM32(pin_va);
    if (hr & 0x80000000u || !enumerator) {
        if (enumerator) vcall(enumerator, 2, 0, NULL);
        hle_free(pin_mem);
        hle_free(fetched_mem);
        return (hr & 0x80000000u) ? hr : E_UNEXPECTED;
    }

    uint32_t result = S_OK;
    for (;;) {
        MEM32(pin_va) = 0;
        MEM32(fetched_va) = 0;
        uint32_t next_args[3] = { 1, pin_va, fetched_va };
        hr = vcall(enumerator, 3, 3, next_args);
        uint32_t pin = MEM32(pin_va);
        uint32_t fetched = MEM32(fetched_va);
        if (hr & 0x80000000u) {
            if (pin) vcall(pin, 2, 0, NULL);
            result = hr;
            break;
        }
        if (!fetched) {
            if (pin) vcall(pin, 2, 0, NULL);
            break;
        }
        if (!pin) {
            result = E_UNEXPECTED;
            break;
        }

        uint32_t *peer_mem = (uint32_t *)hle_alloc(4);
        if (!peer_mem) {
            result = E_OUTOFMEMORY;
            vcall(pin, 2, 0, NULL);
            break;
        }
        uint32_t peer_va = hle_va(peer_mem);
        MEM32(peer_va) = 0;
        uint32_t connected_args[1] = { peer_va };
        uint32_t connected_hr = vcall(pin, 6, 1, connected_args);
        uint32_t peer = MEM32(peer_va);
        hle_free(peer_mem);

        if (connected_hr == S_OK) {
            if (!peer) {
                result = E_UNEXPECTED;
                vcall(pin, 2, 0, NULL);
                break;
            }
            uint32_t disconnect_hr = vcall(pin, 5, 0, NULL);
            if (!(disconnect_hr & 0x80000000u))
                disconnect_hr = vcall(peer, 5, 0, NULL);
            vcall(peer, 2, 0, NULL);
            if (disconnect_hr & 0x80000000u) {
                result = disconnect_hr;
                vcall(pin, 2, 0, NULL);
                break;
            }
        } else if (connected_hr == VFW_E_NOT_CONNECTED) {
            if (peer) vcall(peer, 2, 0, NULL);
        } else {
            if (peer) vcall(peer, 2, 0, NULL);
            result = (connected_hr & 0x80000000u) ? connected_hr : E_UNEXPECTED;
            vcall(pin, 2, 0, NULL);
            break;
        }
        vcall(pin, 2, 0, NULL);
    }

    vcall(enumerator, 2, 0, NULL);
    hle_free(pin_mem);
    hle_free(fetched_mem);
    if (result & 0x80000000u) return result;

    uint32_t no_clock[1] = { 0 };
    hr = vcall(filter, 8, 1, no_clock);
    return (hr & 0x80000000u) ? hr : S_OK;
}
static void graph_RemoveFilter(void) {
    obj_t *g = self(OBJ_GRAPH, "RemoveFilter");
    uint32_t graph_va = g->va, filter = A32(1);
    if (!filter) RET(E_POINTER, 2);

    int index = -1;
    for (int i = 0; i < g->nf; i++)
        if (g->filter[i] == filter) { index = i; break; }
    if (index < 0) RET(E_FAIL, 2);
    if (g->tearing_down || g->teardown_failed) RET(VFW_E_WRONG_STATE, 2);
    for (uint32_t k = 0; k < g->removing_count; k++)
        if (g->removing_filter[k] == filter) RET(VFW_E_WRONG_STATE, 2);
    if (g->removing_count >= MAX_FILTERS) RET(E_OUTOFMEMORY, 2);
    g->refs++;
    g->removing_filter[g->removing_count++] = filter;
    com_addref(filter);
#define REMOVE_FILTER_RET(value) do { \
        uint32_t _remove_hr = (value); \
        obj_t *_remove_graph = obj_of(graph_va, OBJ_GRAPH); \
        if (_remove_graph) { \
            for (uint32_t _k = 0; _k < _remove_graph->removing_count; _k++) { \
                if (_remove_graph->removing_filter[_k] == filter) { \
                    for (uint32_t _j = _k; _j + 1 < _remove_graph->removing_count; _j++) \
                        _remove_graph->removing_filter[_j] = _remove_graph->removing_filter[_j + 1]; \
                    _remove_graph->removing_filter[--_remove_graph->removing_count] = 0; \
                    break; \
                } \
            } \
        } \
        com_release(filter); \
        graph_release(graph_va); \
        RET(_remove_hr, 2); \
    } while (0)

    /* Cache the source-filter classification while the graph still owns the
     * filter. The per-frame Stop compatibility path depends on an exact count;
     * decrement only after removal has completed successfully. */
    int is_asf_reader = filter_is_asf_reader(filter);
    if (is_asf_reader < 0) REMOVE_FILTER_RET(E_OUTOFMEMORY);

    /* Match the Filter Graph Manager's removal sequence: enumerate this
     * filter's pins, disconnect both ends of each connection, then notify
     * the filter that it has left the graph. Disconnect can fail while a
     * filter is running; in that case retain the graph's ownership. */
    uint32_t *pin_mem = (uint32_t *)hle_alloc(4);
    uint32_t *fetched_mem = (uint32_t *)hle_alloc(4);
    uint32_t *peer_mem = (uint32_t *)hle_alloc(4);
    if (!pin_mem || !fetched_mem || !peer_mem) {
        if (pin_mem) hle_free(pin_mem);
        if (fetched_mem) hle_free(fetched_mem);
        if (peer_mem) hle_free(peer_mem);
        REMOVE_FILTER_RET(E_OUTOFMEMORY);
    }

    uint32_t pin_va = hle_va(pin_mem);
    uint32_t fetched_va = hle_va(fetched_mem);
    uint32_t peer_va = hle_va(peer_mem);
    uint32_t enum_out[1] = { 0 };
    uint32_t hr = vcall(filter, 10, 1, enum_out);        /* IBaseFilter::EnumPins */
    uint32_t pin_enum = enum_out[0];

    if (!(hr & 0x80000000u) && pin_enum) {
        for (;;) {
            MEM32(pin_va) = 0;
            MEM32(fetched_va) = 0;
            uint32_t next_args[3] = { 1, pin_va, fetched_va };
            hr = vcall(pin_enum, 3, 3, next_args);       /* IEnumPins::Next */
            uint32_t fetched = MEM32(fetched_va);
            uint32_t pin = MEM32(pin_va);

            if (hr == 1u || (!fetched && !(hr & 0x80000000u))) {
                if (pin) com_release(pin);
                hr = S_OK;                              /* end of enumeration */
                break;
            }
            if (hr & 0x80000000u) {
                if (pin) com_release(pin);
                break;
            }
            if (!pin) { hr = E_UNEXPECTED; break; }

            MEM32(peer_va) = 0;
            uint32_t peer_args[1] = { peer_va };
            uint32_t connected_hr = vcall(pin, 6, 1, peer_args); /* IPin::ConnectedTo */
            uint32_t peer = MEM32(peer_va);
            if (connected_hr == S_OK) {
                if (!peer) {
                    com_release(pin);
                    hr = E_UNEXPECTED;
                    break;
                }
                uint32_t no_args[1] = { 0 };
                uint32_t disconnect_hr = vcall(peer, 5, 0, no_args); /* peer IPin::Disconnect */
                com_release(peer);
                if (disconnect_hr & 0x80000000u) {
                    com_release(pin);
                    hr = disconnect_hr;
                    break;
                }
                disconnect_hr = vcall(pin, 5, 0, no_args); /* this IPin::Disconnect */
                if (disconnect_hr & 0x80000000u) {
                    com_release(pin);
                    hr = disconnect_hr;
                    break;
                }
            } else if (connected_hr == VFW_E_NOT_CONNECTED) {
                /* A pin with no peer is expected when removing a filter;
                 * ConnectedTo documents VFW_E_NOT_CONNECTED for that case. */
                if (peer) com_release(peer);
            } else {
                if (peer) com_release(peer);
                com_release(pin);
                hr = (connected_hr & 0x80000000u) ? connected_hr : E_UNEXPECTED;
                break;
            }

            com_release(pin);
        }
        com_release(pin_enum);
    } else {
        if (pin_enum) com_release(pin_enum);
        if (!(hr & 0x80000000u)) hr = E_UNEXPECTED;
    }

    hle_free(pin_mem);
    hle_free(fetched_mem);
    hle_free(peer_mem);
    if (hr & 0x80000000u) REMOVE_FILTER_RET(hr);

    /* A guest method may re-enter the graph; re-find the entry before
     * mutating the host-side collection. */
    g = obj_of(graph_va, OBJ_GRAPH);
    if (!g) REMOVE_FILTER_RET(E_UNEXPECTED);
    index = -1;
    for (int i = 0; i < g->nf; i++)
        if (g->filter[i] == filter) { index = i; break; }
    if (index < 0) REMOVE_FILTER_RET(S_OK);

    uint32_t clock_args[1] = { 0 };
    uint32_t clock_hr = vcall(filter, 8, 1, clock_args); /* IMediaFilter::SetSyncSource(NULL) */
    if (clock_hr & 0x80000000u) REMOVE_FILTER_RET(clock_hr);
    uint32_t none[2] = { 0, 0 };
    hr = vcall(filter, 13, 2, none);                    /* IBaseFilter::JoinFilterGraph(NULL, NULL) */
    if (hr & 0x80000000u) REMOVE_FILTER_RET(hr);

    g = obj_of(graph_va, OBJ_GRAPH);
    if (!g) REMOVE_FILTER_RET(E_UNEXPECTED);
    index = -1;
    for (int i = 0; i < g->nf; i++)
        if (g->filter[i] == filter) { index = i; break; }
    if (index < 0) REMOVE_FILTER_RET(S_OK);

    uint32_t name = g->fname[index];
    for (int i = index; i + 1 < g->nf; i++) {
        g->filter[i] = g->filter[i + 1];
        g->fname[i] = g->fname[i + 1];
    }
    g->nf--;
    g->filter[g->nf] = 0;
    g->fname[g->nf] = 0;
    g->graph_version++;
    if (is_asf_reader && g->asf_readers > 0) g->asf_readers--;
    if (name) hle_free(hle_ptr(name));
    com_release(filter);                                /* drop the graph's reference */
    REMOVE_FILTER_RET(S_OK);
#undef REMOVE_FILTER_RET
}

static void graph_EnumFilters(void) {
    obj_t *g = self(OBJ_GRAPH, "EnumFilters");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = 0;
    enum_filters_t *e = enum_filters_new(g, NULL);
    if (!e) RET(E_OUTOFMEMORY, 2);
    MEM32(out) = e->va;
    RET(S_OK, 2);
}

static void graph_FindFilterByName(void) {
    obj_t *g = self(OBJ_GRAPH, "FindFilterByName");
    uint32_t pname = A32(1), out = A32(2);
    if (!out) RET(E_POINTER, 3);
    MEM32(out) = 0;
    if (!pname) RET(E_POINTER, 3);
    const uint16_t *wanted = (const uint16_t *)hle_ptr(pname);
    for (int i = 0; i < g->nf; i++) {
        const uint16_t *name = (const uint16_t *)hle_ptr(g->fname[i]);
        size_t j = 0;
        while (name[j] && wanted[j] && name[j] == wanted[j]) j++;
        if (!name[j] && !wanted[j]) {
            uint32_t filter = g->filter[i];
            com_addref(filter);
            MEM32(out) = filter;
            RET(S_OK, 3);
        }
    }
    RET(VFW_E_NOT_FOUND, 3);
}

/* ---- the capture graph builder: ICaptureGraphBuilder2 ---- */
static const guid_t *const g_builder_ok[] = { &IID_IUnknown_, &IID_ICaptureGraphBuilder2_, NULL };
static const named_iid g_none[] = { { NULL, {0} } };

static void builder_QueryInterface(void) {
    obj_t *o = self(OBJ_BUILDER, "QueryInterface");
    RET(query(o, A32(1), A32(2)), 3);
}
static void builder_AddRef(void) { obj_t *o = self(OBJ_BUILDER, "AddRef"); RET((uint32_t)++o->refs, 1); }
static void builder_Release(void) {
    obj_t *o = self(OBJ_BUILDER, "Release");
    int n = --o->refs;
    if (!n) {
        uint32_t graph = o->graph;
        obj_free(o);
        if (graph) com_release(graph);
    }
    RET((uint32_t)n, 1);
}
static void builder_SetFiltergraph(void) {              /* (this, IGraphBuilder *graph) */
    obj_t *o = self(OBJ_BUILDER, "SetFiltergraph");
    uint32_t graph = A32(1);
    if (!graph) RET(E_POINTER, 2);
    if (o->graph) RET(E_UNEXPECTED, 2);                 /* a graph is already set */
    com_addref(graph);
    o = self(OBJ_BUILDER, "SetFiltergraph");
    o->graph = graph;
    fprintf(stderr, "[dshow] CaptureGraphBuilder2 0x%08X: SetFiltergraph(0x%08X) -> S_OK\n", o->va, graph);
    RET(S_OK, 2);
}

/* ---- the ASF writer, for the start-up probe only: IBaseFilter ---- */
static const guid_t *const g_writer_ok[] = { &IID_IUnknown_, &IID_IPersist_, &IID_IMediaFilter_, &IID_IBaseFilter_, NULL };

static void writer_QueryInterface(void) {
    obj_t *o = self(OBJ_WRITER, "QueryInterface");
    RET(query(o, A32(1), A32(2)), 3);
}
static void writer_AddRef(void) { obj_t *o = self(OBJ_WRITER, "AddRef"); RET((uint32_t)++o->refs, 1); }
static void writer_Release(void) {
    obj_t *o = self(OBJ_WRITER, "Release");
    int n = --o->refs;
    if (!n) {
        if (o->sync_source_va) com_release(o->sync_source_va);
        obj_free(o);
    }
    RET((uint32_t)n, 1);
}
static void writer_SetSyncSource(void) {
    obj_t *o = self(OBJ_WRITER, "SetSyncSource");
    uint32_t clock = A32(1);
    if (clock) com_addref(clock);
    if (o->sync_source_va) com_release(o->sync_source_va);
    o->sync_source_va = clock;
    RET(S_OK, 2);
}
static void writer_GetSyncSource(void) {
    obj_t *o = self(OBJ_WRITER, "GetSyncSource");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = o->sync_source_va;
    if (o->sync_source_va) com_addref(o->sync_source_va);
    RET(S_OK, 2);
}
static void writer_GetClassID(void) {                   /* (this, CLSID *) */
    self(OBJ_WRITER, "GetClassID");
    if (!A32(1)) RET(E_POINTER, 2);
    memcpy(hle_ptr(A32(1)), CLSID_WMAsfWriter_, 16);
    RET(S_OK, 2);
}
static void writer_GetState(void) {                     /* (this, DWORD timeout, FILTER_STATE *) */
    self(OBJ_WRITER, "GetState");
    if (!A32(2)) RET(E_POINTER, 3);
    MEM32(A32(2)) = 0;                                  /* State_Stopped: it never runs */
    RET(S_OK, 3);
}
static void writer_SetSyncSource(void) {
    obj_t *o = self(OBJ_WRITER, "SetSyncSource");
    uint32_t clock = A32(1);
    if (clock) com_addref(clock);
    if (o->sync_source_va) com_release(o->sync_source_va);
    o->sync_source_va = clock;
    RET(S_OK, 2);
}
static void writer_GetSyncSource(void) {
    obj_t *o = self(OBJ_WRITER, "GetSyncSource");
    uint32_t out = A32(1);
    if (!out) RET(E_POINTER, 2);
    MEM32(out) = o->sync_source_va;
    if (o->sync_source_va) com_addref(o->sync_source_va);
    RET(S_OK, 2);
}
static void writer_JoinFilterGraph(void) {              /* (this, IFilterGraph *graph, LPCWSTR name) */
    obj_t *o = self(OBJ_WRITER, "JoinFilterGraph");
    uint32_t graph = A32(1), pname = A32(2);
    o->joined = graph;                                  /* not referenced: the graph owns the filter */
    memset(o->name, 0, sizeof o->name);
    if (graph && pname) {
        const uint16_t *src = (const uint16_t *)hle_ptr(pname);
        for (int k = 0; k < MAX_FILTER_NAME - 1 && src[k]; k++) o->name[k] = src[k];
    }
    RET(S_OK, 3);
}
static void writer_QueryFilterInfo(void) {              /* (this, FILTER_INFO *) */
    obj_t *o = self(OBJ_WRITER, "QueryFilterInfo");
    uint32_t info = A32(1);
    if (!info) RET(E_POINTER, 2);
    memcpy(hle_ptr(info), o->name, MAX_FILTER_NAME * 2);
    MEM32(info + MAX_FILTER_NAME * 2) = o->joined;
    uint32_t joined = o->joined;
    if (joined) com_addref(joined);                     /* the caller releases pGraph */
    RET(S_OK, 2);
}

/* ---- class factories (hle_com_register_class) ---- */
static uint32_t create(int kind, const uint8_t *iid, uint32_t *out) {
    char g[40];
    *out = 0;
    hle_guid_string(iid, g);
    if (A32(1)) return CLASS_E_NOAGGREGATION;           /* CoCreateInstance's pUnkOuter: no aggregation */
    obj_t *o = obj_new(kind);
    if (!o) return E_OUTOFMEMORY;
    uint32_t hr = has_iface(o, iid);                    /* the creation reference is the caller's */
    if (hr) {
        obj_free(o);
        fprintf(stderr, "[dshow] CoCreateInstance(%s, %s) -> 0x%08X\n", g_kind_name[kind], g, hr);
        return hr;
    }
    if (kind == OBJ_GRAPH &&
        (!memcmp(iid, IID_IMediaFilter_, 16) || !memcmp(iid, IID_IPersist_, 16))) {
        *out = graph_media_filter(o);
        if (!*out) { obj_free(o); return E_OUTOFMEMORY; }
    } else {
        *out = o->va;
    }
    fprintf(stderr, "[dshow] CoCreateInstance(%s, %s) -> S_OK, interface 0x%08X\n", g_kind_name[kind], g, *out);
    return S_OK;
}
static uint32_t create_graph(const uint8_t *iid, uint32_t *out)   { return create(OBJ_GRAPH, iid, out); }
static uint32_t create_builder(const uint8_t *iid, uint32_t *out) { return create(OBJ_BUILDER, iid, out); }
static uint32_t create_writer(const uint8_t *iid, uint32_t *out)  { return create(OBJ_WRITER, iid, out); }

/* CaptureGraphBuilder2 cannot connect a stream until an ASF reader and
 * renderer pins exist. Fail through HRESULT so callers can take a fallback. */
static void builder_RenderStream(void) {                 /* (this, category, type, source, compressor, renderer) */
    self(OBJ_BUILDER, "RenderStream");
    if (!A32(3)) RET(E_POINTER, 6);
    fprintf(stderr, "[dshow] CaptureGraphBuilder2::RenderStream: stream graph is unavailable\n");
    RET(VFW_E_CANNOT_RENDER, 6);
}

/* ---- vtables: implemented methods, and self-naming stubs for the rest ---- */
static const char *const graph_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "AddFilter", "RemoveFilter", "EnumFilters", "FindFilterByName", "ConnectDirect", "Reconnect", "Disconnect",
    "SetDefaultSyncSource",                                                     /* IFilterGraph */
    "Connect", "Render", "RenderFile", "AddSourceFilter", "SetLogFile", "Abort", "ShouldOperationContinue",
    NULL };                                                                     /* IGraphBuilder */
static const char *const builder_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "SetFiltergraph", "GetFiltergraph", "SetOutputFileName", "FindInterface", "RenderStream", "ControlStream",
    "AllocCapFile", "CopyCaptureFile", "FindPin", NULL };
static const char *const enum_filters_methods[] = {
    "QueryInterface", "AddRef", "Release", "Next", "Skip", "Reset", "Clone", NULL };
static const char *const media_filter_methods[] = {
    "QueryInterface", "AddRef", "Release", "GetClassID", "Stop", "Pause", "Run",
    "GetState", "SetSyncSource", "GetSyncSource", NULL };
static const char *const event_sink_methods[] = {
    "QueryInterface", "AddRef", "Release", "Notify", NULL };
static const char *const media_control_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "GetTypeInfoCount", "GetTypeInfo", "GetIDsOfNames", "Invoke",
    "Run", "Pause", "Stop", "GetState", "RenderFile", "AddSourceFilter",
    "get_FilterCollection", "get_RegFilterCollection", "StopWhenReady", NULL };
static const char *const media_position_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "GetTypeInfoCount", "GetTypeInfo", "GetIDsOfNames", "Invoke",
    "get_Duration", "put_CurrentPosition", "get_CurrentPosition",
    "get_StopTime", "put_StopTime", "get_PrerollTime", "put_PrerollTime",
    "put_Rate", "get_Rate", "CanSeekForward", "CanSeekBackward", NULL };
static const char *const basic_audio_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "GetTypeInfoCount", "GetTypeInfo", "GetIDsOfNames", "Invoke",
    "put_Volume", "get_Volume", "put_Balance", "get_Balance", NULL };
static const char *const basic_video_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "GetTypeInfoCount", "GetTypeInfo", "GetIDsOfNames", "Invoke",
    "get_AvgTimePerFrame", "get_BitRate", "get_BitErrorRate",
    "get_VideoWidth", "get_VideoHeight",
    "put_SourceLeft", "get_SourceLeft", "put_SourceWidth", "get_SourceWidth",
    "put_SourceTop", "get_SourceTop", "put_SourceHeight", "get_SourceHeight",
    "put_DestinationLeft", "get_DestinationLeft",
    "put_DestinationWidth", "get_DestinationWidth",
    "put_DestinationTop", "get_DestinationTop",
    "put_DestinationHeight", "get_DestinationHeight",
    "SetSourcePosition", "GetSourcePosition", "SetDefaultSourcePosition",
    "SetDestinationPosition", "GetDestinationPosition", "SetDefaultDestinationPosition",
    "GetVideoSize", "GetVideoPaletteEntries", "GetCurrentImage",
    "IsUsingDefaultSource", "IsUsingDefaultDestination", NULL };
static const char *const writer_methods[] = {
    "QueryInterface", "AddRef", "Release",
    "GetClassID",                                                               /* IPersist */
    "Stop", "Pause", "Run", "GetState", "SetSyncSource", "GetSyncSource",       /* IMediaFilter */
    "EnumPins", "FindPin", "QueryFilterInfo", "JoinFilterGraph", "QueryVendorInfo", NULL };   /* IBaseFilter */

static const win32hle_shim g_impl[] = {
    { "FilterGraph::QueryInterface",               graph_QueryInterface },
    { "FilterGraph::AddRef",                       graph_AddRef },
    { "FilterGraph::Release",                      graph_Release },
    { "FilterGraph::AddFilter",                    graph_AddFilter },
    { "FilterGraph::RemoveFilter",                 graph_RemoveFilter },
    { "FilterGraph::EnumFilters",                  graph_EnumFilters },
    { "FilterGraph::FindFilterByName",             graph_FindFilterByName },
    { "FilterGraph::Connect",                      graph_Connect },
    { "FilterGraph::ConnectDirect",                 graph_ConnectDirect },
    { "FilterGraph::Reconnect",                    graph_Reconnect },
    { "FilterGraph::Disconnect",                   graph_Disconnect },
    { "FilterGraph::RenderFile",                     graph_RenderFile },
    { "FilterGraph::AddSourceFilter",                graph_AddSourceFilter },
    { "FilterGraphMediaControl::RenderFile",           media_control_RenderFile },
    { "FilterGraphMediaControl::AddSourceFilter",      media_control_AddSourceFilter },
    { "CaptureGraphBuilder2::RenderStream",            builder_RenderStream },
    { "FilterGraph::Render",                        graph_Render },
    { "FilterGraphEnumFilters::QueryInterface",    enum_filters_QueryInterface },
    { "FilterGraphEnumFilters::AddRef",            enum_filters_AddRef },
    { "FilterGraphEnumFilters::Release",           enum_filters_Release },
    { "FilterGraphEnumFilters::Next",              enum_filters_Next },
    { "FilterGraphEnumFilters::Skip",              enum_filters_Skip },
    { "FilterGraphEnumFilters::Reset",              enum_filters_Reset },
    { "FilterGraphEnumFilters::Clone",              enum_filters_Clone },
    { "FilterGraphMediaFilter::QueryInterface",    media_filter_QueryInterface },
    { "FilterGraphMediaFilter::AddRef",            media_filter_AddRef },
    { "FilterGraphMediaFilter::Release",           media_filter_Release },
    { "FilterGraphMediaFilter::GetClassID",        media_filter_GetClassID },
    { "FilterGraphMediaFilter::Stop",              media_filter_Stop },
    { "FilterGraphMediaFilter::Pause",             media_filter_Pause },
    { "FilterGraphMediaFilter::Run",               media_filter_Run },
    { "FilterGraphMediaFilter::GetState",           media_filter_GetState },
    { "FilterGraphMediaFilter::SetSyncSource",      media_filter_SetSyncSource },
    { "FilterGraphMediaFilter::GetSyncSource",      media_filter_GetSyncSource },
    { "FilterGraph::SetDefaultSyncSource",          graph_SetDefaultSyncSource },
    { "FilterGraphEventSink::QueryInterface",       event_sink_QueryInterface },
    { "FilterGraphEventSink::AddRef",               event_sink_AddRef },
    { "FilterGraphEventSink::Release",              event_sink_Release },
    { "FilterGraphEventSink::Notify",               event_sink_Notify },
    { "FilterGraphMediaControl::QueryInterface",     media_control_QueryInterface },
    { "FilterGraphMediaControl::AddRef",             media_control_AddRef },
    { "FilterGraphMediaControl::Release",            media_control_Release },
    { "FilterGraphMediaControl::Run",                media_control_Run },
    { "FilterGraphMediaControl::Pause",              media_control_Pause },
    { "FilterGraphMediaControl::Stop",               media_control_Stop },
    { "FilterGraphMediaControl::GetState",            media_control_GetState },
    { "FilterGraphMediaPosition::QueryInterface",      media_position_QueryInterface },
    { "FilterGraphMediaPosition::AddRef",              media_position_AddRef },
    { "FilterGraphMediaPosition::Release",             media_position_Release },
    { "FilterGraphMediaPosition::get_Duration",         media_position_get_Duration },
    { "FilterGraphMediaPosition::put_CurrentPosition",  media_position_put_CurrentPosition },
    { "FilterGraphMediaPosition::get_CurrentPosition",  media_position_get_CurrentPosition },
    { "FilterGraphMediaPosition::put_StopTime",         media_position_put_StopTime },
    { "FilterGraphMediaPosition::get_StopTime",         media_position_get_StopTime },
    { "FilterGraphMediaPosition::put_PrerollTime",      media_position_put_PrerollTime },
    { "FilterGraphMediaPosition::get_PrerollTime",      media_position_get_PrerollTime },
    { "FilterGraphMediaPosition::put_Rate",             media_position_put_Rate },
    { "FilterGraphMediaPosition::get_Rate",             media_position_get_Rate },
    { "FilterGraphMediaPosition::CanSeekForward",       media_position_CanSeekForward },
    { "FilterGraphMediaPosition::CanSeekBackward",      media_position_CanSeekBackward },
    { "FilterGraphBasicAudio::QueryInterface",          basic_audio_QueryInterface },
    { "FilterGraphBasicAudio::AddRef",                  basic_audio_AddRef },
    { "FilterGraphBasicAudio::Release",                 basic_audio_Release },
    { "FilterGraphBasicAudio::put_Volume",              basic_audio_put_Volume },
    { "FilterGraphBasicAudio::get_Volume",              basic_audio_get_Volume },
    { "FilterGraphBasicAudio::put_Balance",             basic_audio_put_Balance },
    { "FilterGraphBasicAudio::get_Balance",             basic_audio_get_Balance },
    { "FilterGraphBasicVideo::QueryInterface",          basic_video_QueryInterface },
    { "FilterGraphBasicVideo::AddRef",                  basic_video_AddRef },
    { "FilterGraphBasicVideo::Release",                 basic_video_Release },
    { "FilterGraphBasicVideo::get_AvgTimePerFrame",     basic_video_get_AvgTimePerFrame },
    { "FilterGraphBasicVideo::get_VideoWidth",          basic_video_get_VideoWidth },
    { "FilterGraphBasicVideo::get_VideoHeight",         basic_video_get_VideoHeight },
    { "FilterGraphBasicVideo::GetVideoSize",             basic_video_GetVideoSize },
    { "CaptureGraphBuilder2::QueryInterface",      builder_QueryInterface },
    { "CaptureGraphBuilder2::AddRef",              builder_AddRef },
    { "CaptureGraphBuilder2::Release",             builder_Release },
    { "CaptureGraphBuilder2::SetFiltergraph",      builder_SetFiltergraph },
    { "WMAsfWriter::QueryInterface",               writer_QueryInterface },
    { "WMAsfWriter::AddRef",                       writer_AddRef },
    { "WMAsfWriter::Release",                      writer_Release },
    { "WMAsfWriter::GetClassID",                   writer_GetClassID },
    { "WMAsfWriter::GetState",                     writer_GetState },
    { "WMAsfWriter::SetSyncSource",                writer_SetSyncSource },
    { "WMAsfWriter::GetSyncSource",                writer_GetSyncSource },
    { "WMAsfWriter::SetSyncSource",                writer_SetSyncSource },
    { "WMAsfWriter::GetSyncSource",                writer_GetSyncSource },
    { "WMAsfWriter::JoinFilterGraph",              writer_JoinFilterGraph },
    { "WMAsfWriter::QueryFilterInfo",              writer_QueryFilterInfo },
    { 0, 0 }
};

#define MAX_DSHOW_STUB_NAMES 256

static uint32_t make_vtable(const char *iface, const char *const *methods) {
    int n = 0;
    while (methods[n]) n++;
    uint32_t vt = hle_va(hle_alloc(4u * (uint32_t)n));
    for (int k = 0; k < n; k++) {
        static char names[MAX_DSHOW_STUB_NAMES][64];
        static int nn;
        char nm[64];
        snprintf(nm, sizeof nm, "%s::%s", iface, methods[k]);
        uint32_t va = hle_resolve(nm);
        if (!va) {
            if (nn == MAX_DSHOW_STUB_NAMES) hle_fatal("[dshow] more than %d stub names", MAX_DSHOW_STUB_NAMES);
            snprintf(names[nn], sizeof names[nn], "%s", nm);
            va = hle_resolve_or_stub(names[nn++]);      /* calling it stops the run, naming it */
        }
        MEM32(vt + 4u * (uint32_t)k) = va;
    }
    return vt;
}
static void make_vtables(void) {
    if (g_vt[OBJ_GRAPH]) return;
    g_vt[OBJ_GRAPH] = make_vtable("FilterGraph", graph_methods);
    g_vt[OBJ_BUILDER] = make_vtable("CaptureGraphBuilder2", builder_methods);
    g_vt[OBJ_WRITER] = make_vtable("WMAsfWriter", writer_methods);
    g_vt_media_filter = make_vtable("FilterGraphMediaFilter", media_filter_methods);
    g_vt_enum_filters = make_vtable("FilterGraphEnumFilters", enum_filters_methods);
    g_vt_event_sink = make_vtable("FilterGraphEventSink", event_sink_methods);
    g_vt_media_control = make_vtable("FilterGraphMediaControl", media_control_methods);
    g_vt_media_position = make_vtable("FilterGraphMediaPosition", media_position_methods);
    g_vt_basic_audio = make_vtable("FilterGraphBasicAudio", basic_audio_methods);
    g_vt_basic_video = make_vtable("FilterGraphBasicVideo", basic_video_methods);
}

void tm_dshow_init(void) {
    g_ok[OBJ_GRAPH] = g_graph_ok, g_ok[OBJ_BUILDER] = g_builder_ok, g_ok[OBJ_WRITER] = g_writer_ok;
    g_missing[OBJ_GRAPH] = g_graph_missing, g_missing[OBJ_BUILDER] = g_none, g_missing[OBJ_WRITER] = g_writer_missing;
    win32hle_register(g_impl);
    hle_com_register_class(CLSID_FilterGraph_, create_graph);
    hle_com_register_class(CLSID_CaptureGraphBuilder2_, create_builder);
    hle_com_register_class(CLSID_WMAsfWriter_, create_writer);
    tm_dshow_reader_init();
}

int tm_dshow_live(void) {
    int n = 0;
    for (int i = 0; i < MAX_OBJS; i++) n += g_obj[i].va != 0;
    return n;
}

void tm_dshow_report(FILE *f) {
    for (int k = OBJ_GRAPH; k <= OBJ_WRITER; k++)
        if (g_created[k])
            fprintf(f, "[dshow] %s: %d created, %d destroyed\n", g_kind_name[k], g_created[k], g_destroyed[k]);
}
