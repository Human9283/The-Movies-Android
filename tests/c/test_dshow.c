/*
 * The DirectShow start-up probe's objects (src/host/tm_dshow.c): the
 * probe's own sequence (sub_009AD7A0, docs/DIRECTSHOW-STARTUP.md), COM
 * identity and reference counts, the graph/filter relation with a filter of
 * the test's own, the documented error results, and the cases that must
 * stop the run instead of answering. No game files.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "win32hle.h"
#include "host.h"
#include "tm_dshow.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}

#define S_OK                  0x00000000u
#define E_NOINTERFACE         0x80004002u
#define E_POINTER             0x80004003u
#define E_UNEXPECTED          0x8000FFFFu
#define E_FAIL                0x80004005u
#define CLASS_E_NOAGGREGATION 0x80040110u
#define CLSCTX_INPROC_SERVER  1u

typedef uint8_t guid_t[16];
#define G(d1, d2, d3, ...) { (d1) & 0xFF, ((d1) >> 8) & 0xFF, ((d1) >> 16) & 0xFF, ((d1) >> 24) & 0xFF, \
                             (d2) & 0xFF, (d2) >> 8, (d3) & 0xFF, (d3) >> 8, __VA_ARGS__ }
static const guid_t CLSID_FilterGraph          = G(0xE436EBB3, 0x524F, 0x11CE, 0x9F,0x53,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t CLSID_CaptureGraphBuilder2 = G(0xBF87B6E1, 0x8C27, 0x11D0, 0xB3,0xF0,0x00,0xAA,0x00,0x37,0x61,0xC5);
static const guid_t CLSID_WMAsfWriter          = G(0x7C23220E, 0x55BB, 0x11D3, 0x8B,0x16,0x00,0xC0,0x4F,0xB6,0xBD,0x3D);
static const guid_t CLSID_WMAsfReader          = G(0x187463A0, 0x5BB7, 0x11D3, 0xAC,0xBE,0x00,0x80,0xC7,0x5E,0x24,0x6E);
static const guid_t IID_IUnknown        = G(0x00000000, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);
static const guid_t IID_IPersist        = G(0x0000010C, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);
static const guid_t IID_IMediaFilter    = G(0x56A86899, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IBaseFilter     = G(0x56A86895, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IFilterGraph    = G(0x56A8689F, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IGraphBuilder   = G(0x56A868A9, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IMediaControl   = G(0x56A868B1, 0x0AD4, 0x11CE, 0xB0,0x3A,0x00,0x20,0xAF,0x0B,0xA7,0x70);
static const guid_t IID_IFileSinkFilter = G(0xA2104830, 0x7C70, 0x11CF, 0x8B,0xCE,0x00,0xAA,0x00,0xA3,0xF1,0xA6);
static const guid_t IID_ICaptureGraphBuilder2 = G(0x93E5A4E0, 0x2D50, 0x11D2, 0xAB,0xFA,0x00,0xA0,0xC9,0xC6,0xE3,0x8D);
static const guid_t IID_IClassFactory   = G(0x00000001, 0x0000, 0x0000, 0xC0,0,0,0,0,0,0,0x46);

/* ---- guest data ---- */
static uint32_t gcopy(const void *p, uint32_t n) { void *g = hle_alloc(n); memcpy(g, p, n); return hle_va(g); }
static uint32_t gguid(const guid_t g) { return gcopy(g, 16); }
static uint32_t gwstr(const char *s) {
    uint32_t n = (uint32_t)strlen(s) + 1;
    uint16_t *w = hle_alloc(2 * n);
    for (uint32_t k = 0; k < n; k++) w[k] = (uint8_t)s[k];
    return hle_va(w);
}
static uint32_t g_out;                        /* a guest DWORD for out-parameters */

/* ---- calls ---- */
static uint32_t api(const char *name, int n, const uint32_t *a) { return hle_call_guest(hle_resolve(name), n, a); }
static uint32_t vcall(uint32_t obj, int slot, int n, const uint32_t *args) {
    uint32_t a[4] = { obj };
    for (int i = 0; i < n; i++) a[1 + i] = args[i];
    return hle_call_guest(MEM32(MEM32(obj) + 4u * (uint32_t)slot), n + 1, a);
}
static uint32_t addref(uint32_t o)  { return vcall(o, 1, 0, NULL); }
static uint32_t release(uint32_t o) { return vcall(o, 2, 0, NULL); }
static uint32_t qi(uint32_t o, const guid_t iid, uint32_t *out) {
    MEM32(g_out) = 0xDEADBEEFu;
    uint32_t a[2] = { gguid(iid), g_out };
    uint32_t hr = vcall(o, 0, 2, a);
    *out = MEM32(g_out);
    return hr;
}
static uint32_t cocreate(const guid_t clsid, uint32_t outer, const guid_t iid, uint32_t *out) {
    MEM32(g_out) = 0xDEADBEEFu;
    uint32_t a[5] = { gguid(clsid), outer, CLSCTX_INPROC_SERVER, gguid(iid), g_out };
    uint32_t hr = api("CoCreateInstance", 5, a);
    *out = MEM32(g_out);
    return hr;
}
static int in_guest(uint32_t va) { return va >= 0x10000u && va < 0x7F000000u; }
static int vtable_dispatchable(uint32_t obj, int n) {
    uint32_t vt = MEM32(obj);
    if (!in_guest(vt)) return 0;
    for (int k = 0; k < n; k++) if (!recomp_lookup_import(MEM32(vt + 4u * (uint32_t)k))) return 0;
    return 1;
}

/* ---- a filter of the test's own (an IBaseFilter that records its calls) ---- */
static uint32_t tf_refs, tf_joins, tf_graph, tf_name_va, tf_fail_join;
static char tf_name[64];
static void tf_QueryInterface(void) { RET(E_NOINTERFACE, 3); }
static void tf_AddRef(void) { RET(++tf_refs, 1); }
static void tf_Release(void) { RET(--tf_refs, 1); }
static void tf_JoinFilterGraph(void) {        /* (this, graph, name) */
    tf_joins++;
    tf_graph = A32(1), tf_name_va = A32(2);
    tf_name[0] = 0;
    if (A32(2)) { const uint16_t *w = hle_ptr(A32(2)); int k = 0; for (; w[k] && k < 63; k++) tf_name[k] = (char)w[k]; tf_name[k] = 0; }
    RET(tf_fail_join && A32(1) ? E_FAIL : S_OK, 3);
}
static void tf_other(void) { fprintf(stderr, "test filter: unexpected method\n"); exit(9); }
static const win32hle_shim tf_shims[] = {
    { "tf::QueryInterface", tf_QueryInterface }, { "tf::AddRef", tf_AddRef }, { "tf::Release", tf_Release },
    { "tf::JoinFilterGraph", tf_JoinFilterGraph }, { "tf::other", tf_other }, { 0, 0 } };
static uint32_t make_test_filter(void) {
    win32hle_register(tf_shims);
    uint32_t *vt = hle_alloc(15 * 4);
    for (int k = 0; k < 15; k++) vt[k] = hle_resolve("tf::other");
    vt[0] = hle_resolve("tf::QueryInterface"), vt[1] = hle_resolve("tf::AddRef"), vt[2] = hle_resolve("tf::Release");
    vt[13] = hle_resolve("tf::JoinFilterGraph");
    uint32_t *o = hle_alloc(16);
    o[0] = hle_va(vt);
    tf_refs = 1;
    return hle_va(o);
}

/* ---- runs that must stop: in a child, which must exit non-zero ---- */
static uint32_t g_graph_for_child, g_writer_for_child;
static int stops(void (*f)(void)) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        int dn = open("/dev/null", 1); if (dn >= 0) dup2(dn, 2);
        f();
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) != 0) || WIFSIGNALED(st);
}
static void qi_media_control(void) { uint32_t o; qi(g_graph_for_child, IID_IMediaControl, &o); }
static void qi_file_sink(void) { uint32_t o; qi(g_writer_for_child, IID_IFileSinkFilter, &o); }
static void render_file(void) { uint32_t a[2] = { gwstr("x.wmv"), 0 }; vcall(g_graph_for_child, 13, 2, a); }
static void writer_run(void) { uint32_t a[2] = { 0, 0 }; vcall(g_writer_for_child, 6, 2, a); }
static void writer_enum_pins(void) { uint32_t a[1] = { g_out }; vcall(g_writer_for_child, 10, 1, a); }
static void add_twice(void) { uint32_t a[2] = { g_writer_for_child, gwstr("again") }; vcall(g_graph_for_child, 3, 2, a); }
static void add_same_name(void) {
    uint32_t w2; cocreate(CLSID_WMAsfWriter, 0, IID_IBaseFilter, &w2);
    uint32_t a[2] = { w2, gwstr("ASF Enc/Writer") }; vcall(g_graph_for_child, 3, 2, a);
}
static void add_null_name(void) {
    uint32_t w2; cocreate(CLSID_WMAsfWriter, 0, IID_IBaseFilter, &w2);
    uint32_t a[2] = { w2, 0 }; vcall(g_graph_for_child, 3, 2, a);
}
static void call_released(void) { addref(g_writer_for_child); }

int main(void) {
    recomp_host_init();
    tm_dshow_init();
    mach_enter();
    g_out = hle_va(hle_alloc(4));
    uint32_t none[1] = { 0 };

    /* ---- the probe, call by call (sub_009AD7A0) ---- */
    check("CoInitialize(NULL)", api("CoInitialize", 1, none) == S_OK);
    uint32_t graph, builder, writer;
    check("CoCreateInstance(FilterGraph, IGraphBuilder): S_OK", cocreate(CLSID_FilterGraph, 0, IID_IGraphBuilder, &graph) == S_OK);
    check("  an object in guest memory", in_guest(graph));
    check("  its vtable is guest memory, all 18 slots dispatchable", vtable_dispatchable(graph, 18));
    check("CoCreateInstance(CaptureGraphBuilder2, ICaptureGraphBuilder2): S_OK",
          cocreate(CLSID_CaptureGraphBuilder2, 0, IID_ICaptureGraphBuilder2, &builder) == S_OK && in_guest(builder));
    check("  all 12 slots dispatchable", vtable_dispatchable(builder, 12));
    uint32_t sf[1] = { graph };
    check("ICaptureGraphBuilder2::SetFiltergraph(graph): S_OK", vcall(builder, 3, 1, sf) == S_OK);
    check("CoCreateInstance(WMAsfWriter, IBaseFilter): S_OK",
          cocreate(CLSID_WMAsfWriter, 0, IID_IBaseFilter, &writer) == S_OK && in_guest(writer));
    check("  all 15 slots dispatchable", vtable_dispatchable(writer, 15));
    check("three distinct objects", graph != builder && builder != writer && graph != writer);
    uint32_t af[2] = { writer, gwstr("ASF Enc/Writer") };
    check("IFilterGraph::AddFilter(writer, L\"ASF Enc/Writer\"): S_OK", vcall(graph, 3, 2, af) == S_OK);

    /* what the objects now hold: counts read through AddRef/Release */
    check("graph refs 2 (the game's, the builder's)", addref(graph) == 3 && release(graph) == 2);
    check("writer refs 2 (the game's, the graph's)", addref(writer) == 3 && release(writer) == 2);
    check("builder refs 1", addref(builder) == 2 && release(builder) == 1);
    uint32_t info = hle_va(hle_alloc(260));
    uint32_t qfi[1] = { info };
    check("IBaseFilter::QueryFilterInfo: S_OK", vcall(writer, 12, 1, qfi) == S_OK);
    const uint16_t *nm = hle_ptr(info);
    char got[32] = {0}; for (int k = 0; k < 31 && nm[k]; k++) got[k] = (char)nm[k];
    check("  the name the graph gave it", !strcmp(got, "ASF Enc/Writer"));
    check("  pGraph is the graph, with a reference for the caller", MEM32(info + 256) == graph && release(graph) == 2);

    check("Release(writer): 1 (the graph still holds it)", release(writer) == 1);
    check("Release(builder): 0, and it releases the graph", release(builder) == 0 && addref(graph) == 2 && release(graph) == 1);
    check("Release(graph): 0", release(graph) == 0);
    check("  the graph took the writer with it: nothing is alive", tm_dshow_live() == 0);
    api("CoUninitialize", 0, NULL);

    /* ---- COM identity and QueryInterface ---- */
    uint32_t g2, o1, o2;
    cocreate(CLSID_FilterGraph, 0, IID_IUnknown, &g2);
    check("FilterGraph for IUnknown", in_guest(g2));
    check("  QI IFilterGraph: the same pointer, a reference", qi(g2, IID_IFilterGraph, &o1) == S_OK && o1 == g2);
    check("  QI IGraphBuilder: the same pointer", qi(g2, IID_IGraphBuilder, &o2) == S_OK && o2 == g2);
    check("  refs 3", addref(g2) == 4 && release(g2) == 3);
    check("  QI IBaseFilter: E_NOINTERFACE, *ppv NULL", qi(g2, IID_IBaseFilter, &o1) == E_NOINTERFACE && o1 == 0);
    uint32_t qn[2] = { gguid(IID_IUnknown), 0 };
    check("  QI with ppv NULL: E_POINTER", vcall(g2, 0, 2, qn) == E_POINTER);
    release(g2), release(g2);
    check("  two Releases later: 0", release(g2) == 0 && tm_dshow_live() == 0);

    uint32_t w2;
    cocreate(CLSID_WMAsfWriter, 0, IID_IUnknown, &w2);
    check("WMAsfWriter: IPersist, IMediaFilter, IBaseFilter are one object",
          qi(w2, IID_IPersist, &o1) == S_OK && o1 == w2 && qi(w2, IID_IMediaFilter, &o2) == S_OK && o2 == w2 &&
          qi(w2, IID_IBaseFilter, &o1) == S_OK && o1 == w2);
    release(w2), release(w2), release(w2);
    uint32_t clsid = hle_va(hle_alloc(16)), gc[1] = { clsid };
    check("IPersist::GetClassID: CLSID_WMAsfWriter", vcall(w2, 3, 1, gc) == S_OK && !memcmp(hle_ptr(clsid), CLSID_WMAsfWriter, 16));
    uint32_t gst[2] = { 0, g_out };
    MEM32(g_out) = 7;
    check("IMediaFilter::GetState: State_Stopped", vcall(w2, 7, 2, gst) == S_OK && MEM32(g_out) == 0);
    uint32_t gstn[2] = { 0, 0 };
    check("  with NULL: E_POINTER", vcall(w2, 7, 2, gstn) == E_POINTER);
    uint32_t gcn[1] = { 0 };
    check("GetClassID(NULL): E_POINTER", vcall(w2, 3, 1, gcn) == E_POINTER);
    uint32_t qfn[1] = { 0 };
    check("QueryFilterInfo(NULL): E_POINTER", vcall(w2, 12, 1, qfn) == E_POINTER);
    uint32_t info2 = hle_va(hle_alloc(260)), qf2[1] = { info2 };
    MEM32(info2 + 256) = 0xDEADBEEFu;
    check("QueryFilterInfo outside a graph: empty name, pGraph NULL",
          vcall(w2, 12, 1, qf2) == S_OK && MEM16(info2) == 0 && MEM32(info2 + 256) == 0);
    check("last Release: destroyed", release(w2) == 0 && tm_dshow_live() == 0);

    /* ---- CoCreateInstance results ---- */
    uint32_t x;
    check("an interface the class lacks: E_NOINTERFACE, *ppv NULL, nothing kept",
          cocreate(CLSID_CaptureGraphBuilder2, 0, IID_IBaseFilter, &x) == E_NOINTERFACE && x == 0 && tm_dshow_live() == 0);
    check("aggregation: CLASS_E_NOAGGREGATION, nothing kept",
          cocreate(CLSID_FilterGraph, 0x10000000u, IID_IUnknown, &x) == CLASS_E_NOAGGREGATION && x == 0 && tm_dshow_live() == 0);
    check("CLSID_WMAsfReader (the video path) is not registered: REGDB_E_CLASSNOTREG",
          cocreate(CLSID_WMAsfReader, 0, IID_IBaseFilter, &x) == 0x80040154u && x == 0);
    check("IClassFactory is not answered by the objects",
          cocreate(CLSID_FilterGraph, 0, IID_IClassFactory, &x) == E_NOINTERFACE && x == 0 && tm_dshow_live() == 0);

    /* ---- the builder ---- */
    uint32_t b, gg;
    cocreate(CLSID_CaptureGraphBuilder2, 0, IID_ICaptureGraphBuilder2, &b);
    cocreate(CLSID_FilterGraph, 0, IID_IGraphBuilder, &gg);
    uint32_t sn[1] = { 0 }, sg[1] = { gg };
    check("SetFiltergraph(NULL): E_POINTER", vcall(b, 3, 1, sn) == E_POINTER);
    check("SetFiltergraph(graph): S_OK, the graph referenced", vcall(b, 3, 1, sg) == S_OK && addref(gg) == 3 && release(gg) == 2);
    check("SetFiltergraph again: E_UNEXPECTED, no second reference", vcall(b, 3, 1, sg) == E_UNEXPECTED && addref(gg) == 3 && release(gg) == 2);
    release(gg);
    check("the builder's last Release frees the graph too", release(b) == 0 && tm_dshow_live() == 0);

    /* ---- the graph with a filter of the test's own ---- */
    uint32_t f = make_test_filter();
    cocreate(CLSID_FilterGraph, 0, IID_IGraphBuilder, &gg);
    uint32_t an[2] = { 0, gwstr("x") };
    check("AddFilter(NULL): E_POINTER", vcall(gg, 3, 2, an) == E_POINTER);
    tf_fail_join = 1;
    uint32_t a1[2] = { f, gwstr("Test Filter") };
    check("AddFilter when JoinFilterGraph fails: its HRESULT", vcall(gg, 3, 2, a1) == E_FAIL);
    check("  the filter's reference given back", tf_refs == 1);
    tf_fail_join = 0, tf_joins = 0;
    check("AddFilter: S_OK", vcall(gg, 3, 2, a1) == S_OK);
    check("  JoinFilterGraph(graph, name) called once", tf_joins == 1 && tf_graph == gg && !strcmp(tf_name, "Test Filter"));
    check("  the name passed is guest memory the graph keeps", in_guest(tf_name_va) && tf_name_va != a1[1]);
    check("  the graph holds a reference to the filter", tf_refs == 2);
    check("  the filter holds none to the graph", addref(gg) == 2 && release(gg) == 1);
    check("last Release of the graph: JoinFilterGraph(NULL, NULL), then Release of the filter",
          release(gg) == 0 && tf_joins == 2 && tf_graph == 0 && tf_name_va == 0 && tf_refs == 1 && tm_dshow_live() == 0);

    /* ---- what must stop the run ---- */
    cocreate(CLSID_FilterGraph, 0, IID_IGraphBuilder, &g_graph_for_child);
    cocreate(CLSID_WMAsfWriter, 0, IID_IBaseFilter, &g_writer_for_child);
    uint32_t aw[2] = { g_writer_for_child, gwstr("ASF Enc/Writer") };
    vcall(g_graph_for_child, 3, 2, aw);
    check("QI(graph, IMediaControl) stops the run: playback is not implemented", stops(qi_media_control));
    check("QI(writer, IFileSinkFilter) stops the run: writing is not implemented", stops(qi_file_sink));
    check("IGraphBuilder::RenderFile stops the run", stops(render_file));
    check("IMediaFilter::Run on the writer stops the run", stops(writer_run));
    check("IBaseFilter::EnumPins on the writer stops the run (it has no pins)", stops(writer_enum_pins));
    check("AddFilter of a filter already in the graph stops the run", stops(add_twice));
    check("AddFilter of a name already in use stops the run", stops(add_same_name));
    check("AddFilter with a NULL name stops the run", stops(add_null_name));
    release(g_writer_for_child);
    release(g_graph_for_child);
    check("the graph and the writer are gone", tm_dshow_live() == 0);
    check("a method on a destroyed object stops the run", stops(call_released));

    mach_leave();
    printf("dshow: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
