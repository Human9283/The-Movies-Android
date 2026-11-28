/*
 * tm_dshow.c - the DirectShow graph objects used by the start-up probe and
 * the game's video-renderer attachment path (docs/DIRECTSHOW-STARTUP.md).
 *
 * sub_009AD7A0 creates a filter graph, a capture graph builder and the
 * Windows Media ASF Writer filter, gives the graph to the builder, adds the
 * writer to the graph and releases all three. The graph also exposes a
 * distinct graph interfaces for event notification, media control,
 * position, basic audio and basic video. The graph transitions filters, but
 * the ASF reader, pins, media connections and frame delivery remain absent.
 * The startup-probe objects are implemented with real COM ownership rules:
 *
 *   CLSID_FilterGraph          IUnknown, IFilterGraph, IGraphBuilder
 *     AddFilter: keeps a reference to the filter and its name, and calls
 *     its IBaseFilter::JoinFilterGraph(graph, name). The filter does not
 *     reference the graph back. A NULL name, a name already in the graph,
 *     a name of MAX_FILTER_NAME characters or more and a filter already in
 *     the graph stop the run: Windows answers them in ways this file does
 *     not implement (a generated name, VFW_S_DUPLICATE_NAME). The last Release removes every filter
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
 * Threads: the threaded filter graph creates a worker thread on Windows for
 * its events. Nothing here runs a thread or delivers an event, and nothing
 * the probe does needs one.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "win32hle.h"
#include "tm_dshow.h"

#define S_OK                   0x00000000u
#define E_NOINTERFACE          0x80004002u
#define E_POINTER              0x80004003u
#define E_INVALIDARG            0x80070057u
#define E_FAIL                 0x80004005u
#define E_NOTIMPL               0x80004001u
#define VFW_E_CANNOT_RENDER     0x80040218u
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

static const guid_t IID_IUnknown_        = G(0x00000000, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);
static const guid_t IID_IPersist_        = G(0x0000010C, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);
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

/* Interfaces the real objects have and these do not: asking for one stops
 * the run, naming it (all of these GUIDs are in Movies.exe). */
typedef struct { const char *name; guid_t iid; } named_iid;
static const named_iid g_graph_missing[] = {
    { "IFilterGraph2",  G(0x36B73882, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IMediaEvent",    G(0x56A868B6, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaEventEx",  G(0x56A868C0, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaSeeking",  G(0x36B73880, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IVideoWindow",   G(0x56A868B4, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaFilter",   G(0x56A86899, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
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
    uint32_t filter[MAX_FILTERS], fname[MAX_FILTERS];
    /* IMediaEventSink is a distinct COM interface pointer with its own vtable. */
    uint32_t event_sink_va, last_event_code, last_event_param1, last_event_param2;
    uint32_t media_control_va, media_position_va, basic_audio_va, basic_video_va;
    uint32_t event_count;
    uint32_t filter_state, media_loaded;
    double media_duration, media_position, media_stop_time, media_preroll_time;
    double media_rate, avg_time_per_frame;
    int32_t audio_volume, audio_balance;
    /* builder: the graph it was given (referenced) */
    uint32_t graph;
    /* writer: the graph it joined (not referenced) and the name it was given */
    uint32_t joined;
    uint16_t name[MAX_FILTER_NAME];
} obj_t;
#define MAX_OBJS 64
static obj_t g_obj[MAX_OBJS];
static int g_created[4], g_destroyed[4];
static uint32_t g_vt[4];
static uint32_t g_vt_event_sink, g_vt_media_control;
static uint32_t g_vt_media_position, g_vt_basic_audio, g_vt_basic_video;

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
static const guid_t *const g_graph_ok[] = { &IID_IUnknown_, &IID_IFilterGraph_, &IID_IGraphBuilder_, NULL };

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
    if (!memcmp(iid, IID_IMediaControl_, 16))
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
    if (!memcmp(iid, IID_IMediaControl_, 16) ||
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
    int n = --o->refs;
    if (n < 0) hle_fatal("[dshow] FilterGraph 0x%08X: reference count underflow", va);
    if (!n) {
        while (o->nf > 0) {                             /* the last added first */
            int k = --o->nf;
            uint32_t filter = o->filter[k], none[2] = { 0, 0 };
            vcall(filter, 13, 2, none);                 /* IBaseFilter::JoinFilterGraph(NULL, NULL) */
            com_release(filter);
            hle_free(hle_ptr(o->fname[k]));
            o = obj_of(va, OBJ_GRAPH);                  /* the filter's Release may not touch the graph */
            if (!o) hle_fatal("[dshow] FilterGraph 0x%08X vanished while removing its filters", va);
        }
        if (o->event_sink_va) hle_free(hle_ptr(o->event_sink_va));
        if (o->media_control_va) hle_free(hle_ptr(o->media_control_va));
        if (o->media_position_va) hle_free(hle_ptr(o->media_position_va));
        if (o->basic_audio_va) hle_free(hle_ptr(o->basic_audio_va));
        if (o->basic_video_va) hle_free(hle_ptr(o->basic_video_va));
        obj_free(o);
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
    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof filters[0]);
    uint32_t first_failure = S_OK;
    for (int i = 0; i < count; i++) {
        uint32_t args[2] = { start_low, start_high };
        uint32_t hr = vcall(filters[i], method, method == 6 ? 2 : 0, method == 6 ? args : NULL);
        if ((hr & 0x80000000u) && first_failure == S_OK)
            first_failure = hr;
    }
    if (!(first_failure & 0x80000000u))
        o->filter_state = (uint32_t)new_state;
    return first_failure;
}

static void media_control_Run(void) {                    /* (this) */
    obj_t *o = media_control_owner(A32(0));
    /* IMediaControl::Run has no arguments. The graph supplies the future
     * absolute reference time passed to each IMediaFilter::Run(tStart).
     * Use the same monotonic 100-ns time base as the host's clocks, with
     * 100 ms of lead for filters to enter their running state. */
    uint64_t start = hle_mono_ns() / 100u + 1000000u;
    RET(graph_filter_transition(o, 6, 2, (uint32_t)start, (uint32_t)(start >> 32)), 1);
}
static void media_control_Pause(void) {                  /* (this) */
    obj_t *o = media_control_owner(A32(0));
    RET(graph_filter_transition(o, 5, 1, 0, 0), 1);
}
static void media_control_Stop(void) {                   /* (this) */
    obj_t *o = media_control_owner(A32(0));
    RET(graph_filter_transition(o, 4, 0, 0, 0), 1);
}
static void media_control_GetState(void) {               /* (this, timeout, FILTER_STATE *) */
    obj_t *o = media_control_owner(A32(0));
    uint32_t state = A32(2);
    if (!state) RET(E_POINTER, 3);
    MEM32(state) = o->filter_state;
    RET(S_OK, 3);
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

/* The reader/source-filter path is not implemented yet. Return DirectShow's
 * normal render failure instead of invoking a fatal placeholder vtable. */
static void graph_RenderFile(void) {                    /* (this, file, playlist) */
    self(OBJ_GRAPH, "RenderFile");
    if (!A32(1)) RET(E_POINTER, 3);
    fprintf(stderr, "[dshow] FilterGraph::RenderFile: no ASF reader/source filter is registered\\n");
    RET(VFW_E_CANNOT_RENDER, 3);
}
static void graph_AddSourceFilter(void) {               /* (this, file, name, &filter) */
    self(OBJ_GRAPH, "AddSourceFilter");
    uint32_t out = A32(3);
    if (!out) RET(E_POINTER, 4);
    MEM32(out) = 0;
    if (!A32(1)) RET(E_POINTER, 4);
    fprintf(stderr, "[dshow] FilterGraph::AddSourceFilter: no source filter is registered\\n");
    RET(VFW_E_CANNOT_RENDER, 4);
}

/* IGraphBuilder::Render walks the graph's filters and tries their input pins.
 * The filters and enumerators are COM objects owned by the guest, so the host
 * does not assume a pin count, pin name, or media-type layout. */
static void graph_Render(void) {                        /* (this, IPin *out) */
    obj_t *o = self(OBJ_GRAPH, "Render");
    uint32_t graph_va = o->va, out_pin = A32(1);
    if (!out_pin) RET(E_POINTER, 2);

    uint32_t *pin_mem = (uint32_t *)hle_alloc(4);
    uint32_t *fetched_mem = (uint32_t *)hle_alloc(4);
    uint32_t *direction_mem = (uint32_t *)hle_alloc(4);
    if (!pin_mem || !fetched_mem || !direction_mem) {
        if (pin_mem) hle_free(pin_mem);
        if (fetched_mem) hle_free(fetched_mem);
        if (direction_mem) hle_free(direction_mem);
        RET(E_OUTOFMEMORY, 2);
    }
    uint32_t pin_va = hle_va(pin_mem);
    uint32_t fetched_va = hle_va(fetched_mem);
    uint32_t direction_va = hle_va(direction_mem);
    uint32_t filters[MAX_FILTERS];
    int count = o->nf;
    memcpy(filters, o->filter, (size_t)count * sizeof filters[0]);

    uint32_t result = VFW_E_CANNOT_RENDER;
    int connected = 0;
    for (int i = 0; i < count && !connected; i++) {
        uint32_t enumerator = 0;
        uint32_t enum_args[1] = { pin_va };
        MEM32(pin_va) = 0;
        uint32_t hr = vcall(filters[i], 10, 1, enum_args); /* IBaseFilter::EnumPins */
        enumerator = MEM32(enum_args[0]);
        if (hr & 0x80000000u || !enumerator) continue;

        for (;;) {
            MEM32(pin_va) = 0;
            MEM32(fetched_va) = 0;
            uint32_t next_args[3] = { 1, pin_va, fetched_va };
            hr = vcall(enumerator, 3, 3, next_args);       /* IEnumPins::Next */
            uint32_t fetched = MEM32(fetched_va);
            uint32_t pin = MEM32(pin_va);
            if (!fetched || !pin) break;

            MEM32(direction_va) = 0xFFFFFFFFu;
            uint32_t direction_args[1] = { direction_va };
            uint32_t direction_hr = vcall(pin, 9, 1, direction_args); /* IPin::QueryDirection */
            if (!(direction_hr & 0x80000000u) && MEM32(direction_va) == 0) {
                uint32_t connect_args[2] = { pin, 0 };
                uint32_t connect_hr = vcall(out_pin, 3, 2, connect_args); /* IPin::Connect */
                if (!(connect_hr & 0x80000000u)) {
                    result = S_OK;
                    connected = 1;
                } else {
                    result = connect_hr;
                }
            }
            com_release(pin);
            if (connected || (hr & 0x80000000u)) break;
        }
        com_release(enumerator);
    }

    hle_free(pin_mem);
    hle_free(fetched_mem);
    hle_free(direction_mem);
    /* A guest callback may re-enter the graph; verify that it still exists. */
    if (!obj_of(graph_va, OBJ_GRAPH))
        hle_fatal("[dshow] FilterGraph vanished during Render");
    RET(result, 2);
}

static void graph_AddFilter(void) {                     /* (this, IBaseFilter *filter, LPCWSTR name) */
    obj_t *g = self(OBJ_GRAPH, "AddFilter");
    uint32_t f = A32(1), pname = A32(2), va = g->va;
    if (!f) RET(E_POINTER, 3);
    for (int k = 0; k < g->nf; k++)
        if (g->filter[k] == f) hle_fatal("[dshow] FilterGraph::AddFilter(0x%08X): this filter is already in the graph; "
                                          "not implemented here", f);
    if (!pname) hle_fatal("[dshow] FilterGraph::AddFilter(0x%08X, NULL): a generated name is not implemented here", f);
    if (g->nf == MAX_FILTERS) hle_fatal("[dshow] FilterGraph::AddFilter: more than %d filters in one graph", MAX_FILTERS);
    uint16_t w[MAX_FILTER_NAME];
    const uint16_t *src = (const uint16_t *)hle_ptr(pname);
    size_t n = 0;
    while (src[n] && n < MAX_FILTER_NAME - 1) w[n] = src[n], n++;
    w[n] = 0;
    char nm[MAX_FILTER_NAME];
    wname(w, nm, sizeof nm);
    if (src[n]) hle_fatal("[dshow] FilterGraph::AddFilter(\"%s...\"): a name of %d characters or more is not implemented here",
                          nm, MAX_FILTER_NAME);
    if (name_used(g, w)) hle_fatal("[dshow] FilterGraph::AddFilter(\"%s\"): the name is in use; renaming "
                                   "(VFW_S_DUPLICATE_NAME) is not implemented here", nm);
    uint16_t *keep = (uint16_t *)hle_alloc(MAX_FILTER_NAME * 2);
    if (!keep) RET(E_OUTOFMEMORY, 3);
    memcpy(keep, w, (wlen(w) + 1) * 2);
    com_addref(f);
    uint32_t join[2] = { va, hle_va(keep) };
    uint32_t jr = vcall(f, 13, 2, join);                /* IBaseFilter::JoinFilterGraph(graph, name) */
    g = obj_of(va, OBJ_GRAPH);
    if (jr & 0x80000000u || !g) {
        com_release(f);
        hle_free(keep);
        fprintf(stderr, "[dshow] FilterGraph::AddFilter(0x%08X, \"%s\"): JoinFilterGraph -> 0x%08X\n", f, nm, jr);
        RET(g ? jr : E_UNEXPECTED, 3);
    }
    g->filter[g->nf] = f, g->fname[g->nf] = hle_va(keep), g->nf++;
    fprintf(stderr, "[dshow] FilterGraph 0x%08X: AddFilter(0x%08X, \"%s\") -> S_OK, %d filter%s\n", va, f, nm,
            g->nf, g->nf == 1 ? "" : "s");
    RET(S_OK, 3);
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
    if (!n) obj_free(o);                                /* it holds no references */
    RET((uint32_t)n, 1);
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
    *out = o->va;
    fprintf(stderr, "[dshow] CoCreateInstance(%s, %s) -> S_OK, object 0x%08X\n", g_kind_name[kind], g, o->va);
    return S_OK;
}
static uint32_t create_graph(const uint8_t *iid, uint32_t *out)   { return create(OBJ_GRAPH, iid, out); }
static uint32_t create_builder(const uint8_t *iid, uint32_t *out) { return create(OBJ_BUILDER, iid, out); }
static uint32_t create_writer(const uint8_t *iid, uint32_t *out)  { return create(OBJ_WRITER, iid, out); }

/* CaptureGraphBuilder2 cannot connect a stream until an ASF reader and
 * renderer pins exist. Fail through HRESULT so callers can take a fallback. */
static void builder_RenderStream(void) {                 /* (this, category, type, source, compressor, renderer) */
    self(OBJ_BUILDER, "RenderStream");
    fprintf(stderr, "[dshow] CaptureGraphBuilder2::RenderStream: stream graph is unavailable\\n");
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
    { "FilterGraph::Connect",                      graph_Connect },
    { "FilterGraph::ConnectDirect",                 graph_ConnectDirect },
    { "FilterGraph::RenderFile",                     graph_RenderFile },
    { "FilterGraph::AddSourceFilter",                graph_AddSourceFilter },
    { "CaptureGraphBuilder2::RenderStream",            builder_RenderStream },
    { "FilterGraph::Render",                        graph_Render },
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
    { "CaptureGraphBuilder2::QueryInterface",      builder_QueryInterface },
    { "CaptureGraphBuilder2::AddRef",              builder_AddRef },
    { "CaptureGraphBuilder2::Release",             builder_Release },
    { "CaptureGraphBuilder2::SetFiltergraph",      builder_SetFiltergraph },
    { "WMAsfWriter::QueryInterface",               writer_QueryInterface },
    { "WMAsfWriter::AddRef",                       writer_AddRef },
    { "WMAsfWriter::Release",                      writer_Release },
    { "WMAsfWriter::GetClassID",                   writer_GetClassID },
    { "WMAsfWriter::GetState",                     writer_GetState },
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
