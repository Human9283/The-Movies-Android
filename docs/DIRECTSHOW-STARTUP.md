# DirectShow at start-up: the observed boundary

The run currently stops in the game's DirectShow start-up probe. This file
records what the executable does there, what is known about the rest of
its DirectShow use, and how calls will be instrumented when the work
starts. It does not implement DirectShow. Nothing here adds a COM class to
the host.

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

## 3. What the run shows [run]

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

## 5. What passing the probe would take

These are the facts that a later implementation must meet. Nothing is built
now.

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
writer that cannot write. The project rules exclude that, and it is why the
probe stays failing until the writer is real (IMPORTS.md, task C). There is
also an open decision: whether start-up waits for film export, or whether
these three objects can be real and complete for what they claim (graph
bookkeeping, a writer filter with no connected pins) while encoding comes
later. docs/REPOSITORY-AUDIT.md records it.

## 6. Threading

- `CLSID_FilterGraph` is the threaded graph. On Windows it creates a
  worker thread with a hidden window for its event and clock
  notifications. `CLSID_FilterGraphNoThread` does not; its user must pump
  messages on the creating thread [doc.]. The game uses only the threaded
  one (section 4).
- win32hle runs all guest threads under one machine lock. A host-side
  graph thread that calls back into the guest (a filter's methods, an
  event sink) must take the lock as a guest thread does and must not hold
  it while it waits.
- COM apartments are not modelled: `CoInitialize` and `CoInitializeEx`
  succeed and record nothing. The probe runs on the main thread.

## 7. Instrumentation design

When the DirectShow work starts, every object gets a trace before any
behaviour does, so each step is checked against what the game actually
asks for.

**Records.** One JSON line per event, written with the import trace
(`--import-trace`) or on its own (`--com-trace FILE`, planned):

| Event | Fields |
|---|---|
| `create` | CLSID, IID, outer, context, caller VA, `HRESULT`, object VA, class name |
| `qi` | object, IID, result object, `HRESULT`, caller |
| `addref` / `release` | object, count after, caller |
| `call` | object, interface, method name and slot, arguments (raw dwords, and decoded where the type is known: GUIDs, wide strings, `AM_MEDIA_TYPE`), `HRESULT`, caller, thread |
| `destroy` | object, interface, lifetime in calls |
| `graph` | graph object, filter added or removed, name, the filter's CLSID |

**Where.** At the COM boundary only: a class factory registered with
`hle_com_register_class`, and a vtable of shims per interface
(`hle_com_vtable`). Every method of every interface is listed, so a call
to a method with no implementation stops the run and names the interface,
the slot and the caller, as the DirectInput 8 object does. Objects and
vtables live in guest memory (B8).

**Graph relations.** The trace keeps object → interface, filter → graph
and graph → filters maps, so a run's report can print the graph the game
built: filters, names, pins and connections once pins exist.

**Tests.** A C test per class, in the style of `tests/c/test_dinput8.c`:
the probe's six calls in order, reference counts back to zero, `AddFilter`
calling `JoinFilterGraph`, the release order, and the error cases
(`E_POINTER`, `E_NOINTERFACE`, `VFW_E_DUPLICATE_NAME` for a second filter
with the same name [doc.]).

## 8. Sources

- Microsoft Learn: `IFilterGraph::AddFilter`, `IBaseFilter::JoinFilterGraph`,
  `ICaptureGraphBuilder2::SetFiltergraph`, the WM ASF Writer filter,
  `CLSID_FilterGraphNoThread`, `CoInitializeEx` return values. These were
  used for behaviour only.
- Wine's `quartz` and `qcap` (LGPL-2.1) for comparison only. No code was
  read into this repository and none may be copied: the repository is
  MIT. See docs/REPOSITORY-AUDIT.md, "References".
