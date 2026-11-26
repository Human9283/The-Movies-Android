# DirectShow at start-up: the probe and its objects

The game's start-up runs a DirectShow probe before the Direct3D device is
made. This file records what the executable does there, what is known
about the rest of its DirectShow use, and what is implemented
(`src/host/tm_dshow.c`, section 9). Only the probe's contract is
implemented. It is not a DirectShow implementation: nothing plays, nothing
is written, and the ASF writer is not a working filter.

Tags: [static] read from `Movies.exe` 1.1; [run] measured
(`tests/m3_boundary.py`, trace `docs/traces/m3_boundary_imports.json`);
[doc.] Microsoft documentation; [hyp.] not verified.

## 1. Where it is reached

`WinMain` (`0x005303C0`) → `sub_0093C640`, the game's Windows Media check
(called from `0x0093CB78`) → `sub_009AD7A0`, the probe. [static, run]

The check has two parts (IMPORTS.md, "The current boundary"). Part 1
(`wmvcore.dll` and `dinput8.dll` present in the system directory) passes.
Part 2 is the probe.

## 2. What the probe does [static]

Read from the instructions at `0x009AD7A0`–`0x009AD88C`:

| # | Call | Arguments | On failure (`HRESULT < 0`) |
|---|---|---|---|
| 1 | `CoInitialize` | `NULL` | returns false at once, without `CoUninitialize` |
| 2 | `CoCreateInstance` | `CLSID_FilterGraph` (GUID at `0x00D0355C`), `NULL`, `CLSCTX_INPROC_SERVER`, `IID_IGraphBuilder` (`0x00D01E1C`) | jumps to the releases |
| 3 | `CoCreateInstance` | `CLSID_CaptureGraphBuilder2` (`0x00D0359C`), `NULL`, `CLSCTX_INPROC_SERVER`, `IID_ICaptureGraphBuilder2` (`0x00D01DEC`) | releases |
| 4 | `ICaptureGraphBuilder2::SetFiltergraph` (vtable `+0x0C`) | the graph from 2 | releases |
| 5 | `CoCreateInstance` | `CLSID_WMAsfWriter` (`0x00D032DC`), `NULL`, `CLSCTX_INPROC_SERVER`, `IID_IBaseFilter` (`0x00D01F7C`) | releases |
| 6 | `IFilterGraph::AddFilter` (vtable `+0x0C` of the `IGraphBuilder`) | the writer, `L"ASF Enc/Writer"` (`0x00CCBB78`) | releases |
| 7 | result = true | | |
| 8 | `Release` (vtable `+0x08`) of the writer, the capture builder and the graph, each if non-NULL, in that order | | |
| 9 | `CoUninitialize` | | |

It returns the byte from step 7, false unless all six calls succeeded. No
other method is called, and no object outlives the function.

## 3. What the run showed before the objects existed [run]

This is the run before `tm_dshow.c`. Section 9 has the run now.

| Import | Calls | Caller | Result |
|---|---|---|---|
| `CoInitialize` | 2 | `sub_009F9950` (earlier, not the probe), then `0x009AD7A0` | `S_OK` both times |
| `CoCreateInstance` | 1 | `0x009AD7A0`: `CLSID_FilterGraph`, `IID_IGraphBuilder` | `0x80040154` `REGDB_E_CLASSNOTREG` |
| `CoUninitialize` | 1 | `0x009AD7A0` | |

win32hle's `CoCreateInstance` answers `REGDB_E_CLASSNOTREG` for a class no
host registered (`hle_com_register_class`), which is what Windows answers
for a COM server that is not installed. So the probe fails at step 2, as it
should. The check shows its warning box (`MessageBoxW`, type `0x30`), and
`WinMain` takes its exit path.

On Windows a second `CoInitialize` on the same thread returns `S_FALSE`
[doc.]. win32hle returns `S_OK`. The probe tests `>= 0`, so the difference
does not matter here.

## 4. The startup writer probe is not the video path [static]

The probe creates an ASF **writer**. Video playback uses the ASF
**reader**, in another function:

| Function | Called from | CLSIDs and IIDs it references | Entered in the run |
|---|---|---|---|
| `sub_009AD7A0` (start-up probe) | `sub_0093C640` (`0x0093CB78`) | `CLSID_FilterGraph`, `CLSID_CaptureGraphBuilder2`, `CLSID_WMAsfWriter` | yes |
| `sub_0097E190` (video playback) | `sub_0097EA90` (`0x0097EAF5`), `sub_0097EC00` (`0x0097ECC0`) | `CLSID_FilterGraph` + `IID_IGraphBuilder`, `CLSID_WMAsfReader` (`0x00D032EC`), `IID_IFileSourceFilter`, `IID_IMediaControl` | no |
| `sub_0097D270` | `sub_0097EA90` (`0x0097EB79`) | `CLSID_FilterGraph`, `CLSID_CaptureGraphBuilder2`, `IID_IMediaControl` | no |
| `sub_009AD440`, `sub_009B0D30` (film export) | `sub_009B0F90` and `sub_009B1300` (`0x009B12C3`–`0x009B130E`) | `CLSID_FilterGraph`, `CLSID_CaptureGraphBuilder2`; `CLSID_WMAsfWriter` in `sub_009B0D30` | no |
| `sub_009B0000`, `sub_009B0078` (reached through vtables) | | `CLSID_FilterGraph`, `CLSID_CaptureGraphBuilder2` | no |

`CLSID_WMAsfReader` has exactly one code reference (`0x0097E3AB`, in
`sub_0097E190`). `CLSID_FilterGraphNoThread` is in `.rdata` (`0x00D0354C`),
but no code or data refers to its address. [hyp.] It probably came in
with the `strmiids` GUID library.

So the probe needs three objects that can be created and joined, and none
of them plays anything. The probe never touches the reader, its file
source, `IMediaControl` or a renderer.

## 5. What passing the probe takes

These are the facts the implementation (section 9) meets:

- `CLSID_FilterGraph` creatable for `IID_IGraphBuilder`, with an
  `AddFilter` that keeps a reference to the filter and calls its
  `IBaseFilter::JoinFilterGraph(graph, name)`. On the final `Release` it
  removes its filters (`JoinFilterGraph(NULL, NULL)`, then `Release`)
  [doc.].
- `CLSID_CaptureGraphBuilder2` creatable for `IID_ICaptureGraphBuilder2`,
  with a `SetFiltergraph` that keeps a reference to the graph.
- `CLSID_WMAsfWriter` creatable for `IID_IBaseFilter`, with `IUnknown`,
  `IPersist::GetClassID`, `IMediaFilter` state, `JoinFilterGraph` and
  `QueryFilterInfo` behaving as documented.

An object that only answers `S_OK` would pass the probe and give the game a
writer that cannot write. That is excluded. The implementation instead
makes the three objects real for what they claim: graph bookkeeping, and a
writer filter that can be created, named, joined, queried and destroyed.
Everything else stops the run, naming itself. Encoding comes later, as a
separate capability (section 9; IMPORTS.md, task C).

## 6. Threading boundary

The game references `CLSID_FilterGraph`, the threaded graph. Windows uses
an internal worker thread for graph notifications; this implementation
creates no graph worker, hidden window, event queue, or clock. The current
start-up probe does not require them. win32hle serializes guest execution
under its machine lock, and COM apartment state is handled separately by
`tm_ole32.c`. Any future graph worker that calls guest code must enter the
guest execution model safely; that behaviour is not implemented or tested.

## 7. Diagnostics and test coverage

There is no dedicated COM JSON trace or `--com-trace` option in the current
implementation. The start-up path is observable through the existing
`--import-trace`, the DirectShow diagnostics written by
`src/host/tm_dshow.c`, and the focused unit test
`tests/c/test_dshow.c`.

The implementation registers the three classes through
`hle_com_register_class` and exposes guest-memory vtables through
`hle_com_vtable`. Unsupported methods are named diagnostic stops; a
`QueryInterface` request for an interface known to exist on the real object
but absent from this implementation also stops with the interface name.
Ordinary unsupported interfaces return `E_NOINTERFACE`.

The unit test exercises the game's object-creation sequence, COM identity,
reference ownership, graph/filter association, release order, and selected
error cases. It currently reports 63 checks. It does not establish video
playback, ASF encoding, worker-thread event delivery, or full DirectShow
conformance.

## 8. Sources

- Microsoft Learn: `IFilterGraph::AddFilter`, `IBaseFilter::JoinFilterGraph`,
  `ICaptureGraphBuilder2::SetFiltergraph`, the WM ASF Writer filter,
  `CLSID_FilterGraphNoThread`, `CoInitializeEx` return values. These were
  used for behaviour only.
- Wine's `quartz` and `qcap` (LGPL-2.1) for comparison only. No code was
  read into this repository and none may be copied: the repository is
  MIT. See docs/REPOSITORY-AUDIT.md, "References".

## 9. Implemented: start-up probe compatibility

`src/host/tm_dshow.c`; test `tests/c/test_dshow.c` (63 checks).

### Capabilities, kept apart

| Capability | State |
|---|---|
| **A. Start-up probe compatibility**: the three objects of section 2 with real COM identity, reference ownership and the graph/filter relation | **implemented** |
| B. Video playback: `CLSID_WMAsfReader`, file source, `IMediaControl`, the game's renderer filter (`sub_0097E190`) | not implemented. `CLSID_WMAsfReader` is not registered (`REGDB_E_CLASSNOTREG`), and asking the graph for `IMediaControl` stops the run |
| C. Film export: an ASF writer that writes (pins, `IConfigAsfWriter`, `IFileSinkFilter`, the profile, encoding) | not implemented. The writer has no pins, never leaves `State_Stopped`, and asking for those interfaces stops the run |
| D. WMA decoding (the synchronous reader) | not implemented (IMPORTS.md, task D) |

The writer object is **not** a complete DirectShow filter. It implements
the start-up probe's contract, and only that.

### What each object implements

| Object (CLSID) | Interfaces it answers for | Implemented methods | Everything else |
|---|---|---|---|
| filter graph (`CLSID_FilterGraph`) | `IUnknown`, `IFilterGraph`, `IGraphBuilder` | `QueryInterface`, `AddRef`, `Release`, `AddFilter` | the other 14 methods are stubs that stop the run, naming themselves. A QI for `IFilterGraph2`, `IMediaControl`, `IMediaEvent(Ex)`, `IMediaPosition`, `IMediaSeeking`, `IBasicAudio`, `IBasicVideo`, `IVideoWindow` or `IMediaFilter` stops the run |
| capture graph builder (`CLSID_CaptureGraphBuilder2`) | `IUnknown`, `ICaptureGraphBuilder2` | `QueryInterface`, `AddRef`, `Release`, `SetFiltergraph` | 8 stubs |
| ASF writer (`CLSID_WMAsfWriter`) | `IUnknown`, `IPersist`, `IMediaFilter`, `IBaseFilter` | `QueryInterface`, `AddRef`, `Release`, `GetClassID`, `GetState` (always `State_Stopped`), `JoinFilterGraph`, `QueryFilterInfo` | `Stop`, `Pause`, `Run`, `SetSyncSource`, `GetSyncSource`, `EnumPins`, `FindPin`, `QueryVendorInfo` are stubs. A QI for `IConfigAsfWriter(2)`, `IFileSinkFilter(2)`, `IMediaSeeking` or `IAMFilterMiscFlags` stops the run |

Any other interface gets `E_NOINTERFACE` with `*ppv` set to NULL. The
interface lists of what stops the run were checked against the GUIDs
linked into `Movies.exe`: all of them are there.

### Ownership

- `CoCreateInstance` gives the caller the only reference. Aggregation
  gives `CLASS_E_NOAGGREGATION`. An interface the class lacks gives
  `E_NOINTERFACE`, and no object is kept.
- `AddFilter(f, name)` calls `f->AddRef`, then
  `f->JoinFilterGraph(graph, name)`. The name it passes is a copy the
  graph keeps in guest memory. If the join fails, the reference is given
  back and its `HRESULT` returned. The filter does not reference the
  graph.
- The graph's last `Release` calls, for each filter, last added first,
  `JoinFilterGraph(NULL, NULL)` and then `Release`. Then the graph is
  freed.
- `SetFiltergraph(g)` keeps a reference to `g`, and the builder's last
  `Release` gives it back. A second call gives `E_UNEXPECTED`, and NULL
  gives `E_POINTER`.
- `QueryFilterInfo` copies the name and returns `pGraph` with a reference
  for the caller, or NULL outside a graph.
- Filters are called through their own vtables, so a filter may be the
  game's own code.
- A method called on a destroyed object stops the run.

AddFilter cases that stop the run, because Windows answers them in ways
not implemented here: a NULL name (Windows generates one), a name already
in the graph (Windows renames it and returns `VFW_S_DUPLICATE_NAME`), a
name of 128 characters or more, and a filter already in the graph.

### Guest memory

Every object is 16 bytes from `hle_alloc` with its vtable at `[0]`. Each
vtable is an array of shim VAs, also from `hle_alloc`, and is made at the
first `CoCreateInstance`. The guest heap before the probe is therefore laid
out as before. DirectInput's object addresses in the M3 baseline did not
move.

### The run now [run]

```
[dshow] CoCreateInstance(FilterGraph, {56A868A9-...}) -> S_OK, object 0x11251550
[dshow] CoCreateInstance(CaptureGraphBuilder2, {93E5A4E0-...}) -> S_OK, object 0x11251570
[dshow] CaptureGraphBuilder2 0x11251570: SetFiltergraph(0x11251550) -> S_OK
[dshow] CoCreateInstance(WMAsfWriter, {56A86895-...}) -> S_OK, object 0x11251590
[dshow] FilterGraph 0x11251550: AddFilter(0x11251590, "ASF Enc/Writer") -> S_OK, 1 filter
[dshow] CaptureGraphBuilder2 0x11251570 destroyed
[dshow] WMAsfWriter 0x11251590 destroyed
[dshow] FilterGraph 0x11251550 destroyed
```

The probe returns true. The release order is the one read from the
instructions (writer, builder, graph): the writer survives its own
`Release` because the graph still holds it, and goes with the graph. No
object is alive afterwards. The Windows Media warning box no longer
appears, and the exit path that followed it (with `CreateThread`,
`CreateEventA`, `WaitForSingleObject` and the fault in `sub_00936350`) is
no longer reached.

After the probe the game goes on to Direct3D 9 (`d3d9.dll`,
`Direct3DCreate9`). The headless run stops at `CreateDevice`
(D3D9-REQUIREMENTS.md); with `--d3d9 dxvk` the game reaches its menus and
asks DirectShow to play video, which is not implemented (IMPORTS.md, "Past
`CreateDevice`").
