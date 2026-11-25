/*
 * tm_dshow.c - the DirectShow objects of the game's start-up probe
 * (docs/DIRECTSHOW-STARTUP.md).
 *
 * sub_009AD7A0 creates a filter graph, a capture graph builder and the
 * Windows Media ASF Writer filter, gives the graph to the builder, adds the
 * writer to the graph and releases all three. This file implements exactly
 * that contract, with real objects:
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
 * Everything else is unimplemented and stops the run when called, naming
 * itself: every other method of these interfaces (a self-naming stub in
 * the vtable), and QueryInterface for an interface the real object has and
 * this one does not (IMediaControl on the graph, IFileSinkFilter on the
 * writer, ...). QueryInterface for an interface the real object does not
 * have either answers E_NOINTERFACE.
 *
 * COM: an object is 16 bytes of guest memory whose first dword is its
 * vtable, an array of shim VAs, also in guest memory. One vtable serves all
 * of an object's interfaces (each extends the one before), so every
 * interface pointer of an object is the same address and QueryInterface
 * keeps COM identity. Host-side state is found by the guest address. A
 * filter added to the graph is called through its own vtable
 * (hle_call_guest), so it may be guest code. Vtables are made at the first
 * CoCreateInstance, not at start-up, so the guest heap before that point is
 * laid out as without this file.
 *
 * Threads: the threaded filter graph creates a worker thread on Windows for
 * its events. Nothing here runs a thread or delivers an event, and nothing
 * the probe does needs one.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "tm_dshow.h"

#define S_OK                   0x00000000u
#define E_NOINTERFACE          0x80004002u
#define E_POINTER              0x80004003u
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
static const guid_t IID_ICaptureGraphBuilder2_ = G(0x93E5A4E0, 0x2D50, 0x11D2, 0xAB,0xFA,0x00,0xA0,0xC9,0xC6,0xE3,0x8D);

/* Interfaces the real objects have and these do not: asking for one stops
 * the run, naming it (all of these GUIDs are in Movies.exe). */
typedef struct { const char *name; guid_t iid; } named_iid;
static const named_iid g_graph_missing[] = {
    { "IFilterGraph2",  G(0x36B73882, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IMediaControl",  G(0x56A868B1, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaEvent",    G(0x56A868B6, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaEventEx",  G(0x56A868C0, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaPosition", G(0x56A868B2, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IMediaSeeking",  G(0x36B73880, 0xC2C8, 0x11CF, 0x8B,0x46,0x00,0x80,0x5F,0x6C,0xEF,0x60) },
    { "IBasicAudio",    G(0x56A868B3, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
    { "IBasicVideo",    G(0x56A868B5, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70) },
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

static void graph_QueryInterface(void) {                /* (this, riid, ppv) */
    obj_t *o = self(OBJ_GRAPH, "QueryInterface");
    RET(query(o, A32(1), A32(2)), 3);
}
static void graph_AddRef(void) { obj_t *o = self(OBJ_GRAPH, "AddRef"); RET((uint32_t)++o->refs, 1); }
static void graph_Release(void) {
    obj_t *o = self(OBJ_GRAPH, "Release");
    int n = --o->refs;
    if (!n) {
        uint32_t va = o->va;
        while (o->nf > 0) {                             /* the last added first */
            int k = --o->nf;
            uint32_t f = o->filter[k], none[2] = { 0, 0 };
            vcall(f, 13, 2, none);                      /* IBaseFilter::JoinFilterGraph(NULL, NULL) */
            com_release(f);
            hle_free(hle_ptr(o->fname[k]));
            o = obj_of(va, OBJ_GRAPH);                  /* the filter's Release may not touch the graph */
            if (!o) hle_fatal("[dshow] FilterGraph 0x%08X vanished while removing its filters", va);
        }
        obj_free(o);
    }
    RET((uint32_t)n, 1);
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

static uint32_t make_vtable(const char *iface, const char *const *methods) {
    int n = 0;
    while (methods[n]) n++;
    uint32_t vt = hle_va(hle_alloc(4u * (uint32_t)n));
    for (int k = 0; k < n; k++) {
        static char names[64][64];
        static int nn;
        char nm[64];
        snprintf(nm, sizeof nm, "%s::%s", iface, methods[k]);
        uint32_t va = hle_resolve(nm);
        if (!va) {
            if (nn == 64) hle_fatal("[dshow] too many stub names");
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
