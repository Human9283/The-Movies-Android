# Android DirectShow video path

## Scope

The Android port contains a video-only implementation of the game's Windows
Media ASF Reader. It is intended to support the files used during startup and
in the frontend:

- `atvi_logo.wmv`
- `frontend_intro.wmv`
- `frontend_loop.wmv`

The implementation is source code only at this stage. It has **not** been
compiled for Android or exercised against the game. Do not interpret the
presence of this code as confirmation that the splash video, frontend, or
gameplay works.

## Current path

1. The HLE COM registry exposes `CLSID_WMAsfReader`.
2. The reader exposes `IBaseFilter` and `IFileSourceFilter`. `Load` resolves
   the guest path through the HLE game-data mount and opens it with the
   FFmpeg-backed ASF decoder.
3. The source filter exposes one `IPin` named `Video` and enumerates one
   `MEDIATYPE_Video / MEDIASUBTYPE_RGB24 / FORMAT_VideoInfo) media type.
4. The source pin negotiates the downstream renderer's `IMemInputPin` and
   allocator, requests four buffers, and commits/decommits it with the
   filter's Pause/Run/Stop transitions. `IFileSourceFilter` preserves its
   inherited `IPersist::GetClassID` vtable slot before `Load` and `GetCurFile`.
5. The file source starts its streaming worker during the stopped-to-paused
   transition and sends one frame to cue the renderer. The renderer may block
   the worker in `Receive` until the graph runs; the source accounts for that
   paused interval before scheduling later frames.
6. A worker decodes WMV frames to BGR24, converts them to the bottom-up
   RGB24 layout described by `VIDEOINFOHEADER`, timestamps them in
   100-nanosecond units, and calls the single-sample
   `IMemInputPin::Receive` method (not `ReceiveMultiple`). A downstream
   `S_FALSE` rejects that sample; ordinary receive failures stop the stream.
   An allocator `VFW_E_TIMEOUT` is treated as backpressure: the source retries
   the same decoded frame rather than dropping it. An in-progress pin flush
   cancels the current sample and resumes only after `EndFlush`.
7. The reader uses the graph-selected `IReferenceClock` for frame pacing and
   pause accounting. The clock pointer is AddRef-protected while it is handed
   to the worker, so a concurrent `SetSyncSource` cannot release it between
   pointer load and `GetTime`. A system-monotonic fallback is used only when
   the graph has no usable clock.
8. Before the first sample, and again after reopening a looped file, the source
   calls downstream `IPin::NewSegment(0, duration, 1.0)`. If a flush
   interrupts sample submission while playback continues, the worker starts a
   new segment at that frame's media position and rebases sample timestamps to
   zero for the new segment. Its scheduling anchor is rebased in the same clock
   domain while preserving accumulated pause compensation, avoiding a catch-up
   burst or double-counting paused time. The first sample of each segment is marked
   discontinuous. The single reader worker serializes
   `NewSegment`, `Receive`, and `EndOfStream` in source order. The HLE machine
   lock serializes their guest-COM dispatch against graph-thread control
   callbacks. No native mutex is held across guest COM calls: doing so would
   invert the HLE machine-lock order and could deadlock a worker callback
   against `BeginFlush`. `BeginFlush` remains a concurrent cancellation call
   so it can release a blocked downstream `Receive`. Sample allocation,
   metadata setup and the final `pin_flushing` check are performed in one HLE
   machine turn, preventing a flush from slipping between the check and the
   submission of a new sample. A flush-generation counter detects a
   BeginFlush/EndFlush pair even when both complete while a guest call has
   yielded the machine; a cancelled frame is retried only after announcing a
   new segment. The blocking `Receive` can still yield the HLE machine to the
   flush callback while it is in flight.
9. End of stream is forwarded to the connected pin and reported as
   `EC_COMPLETE` only if it still belongs to the active segment. Decoder,
   loop-reopen, segment-negotiation, downstream-delivery, and terminal
   `EndOfStream` failures report `EC_ERRORABORT` with the corresponding
   HRESULT. A Stop/flush cancellation does not masquerade as a media failure.
   Downstream `BeginFlush`, worker-join, and `EndFlush` HRESULTs are
   propagated. A failed `BeginFlush` does not lead to an unbounded join of a
   worker that may still be blocked in `Receive`; a failed `EndFlush` leaves
   the source in its flushing state rather than admitting frames into a
   renderer that did not resume. If a streaming callback re-enters Stop or
   Disconnect on the reader's own worker, the source requests termination but
   does not detach the thread or release its allocator while `Receive` is
   still on the stack. Stop reports an intermediate result; Disconnect/graph
   removal is rejected until the worker can be joined from another thread.
   The worker detaches itself only when its final COM reference is released
   from that worker, after streaming callbacks have unwound.

The exposed reader pin is an output pin. Its incoming `EndOfStream`,
`BeginFlush`, `EndFlush`, and `NewSegment` methods therefore return
`E_UNEXPECTED`; the source performs those operations internally on its
connected downstream input pin. This keeps the public `IPin` direction contract
separate from the source's own streaming control path.

The graph also routes `IGraphBuilder::RenderFile` and
`IGraphBuilder::AddSourceFilter` through this reader. Graph state changes run
through the filters downstream-first so the renderer is ready before the
source starts pushing samples. `IMediaControl::GetState` polls child filter
states using the caller's timeout, including nonblocking zero-timeout polls,
`VFW_S_STATE_INTERMEDIATE`, and `VFW_S_CANT_CUE`. The source forwards
`BeginFlush`/`EndFlush` to the downstream pin; a real source-filter `Stop`
signals the worker, flushes the peer to release a blocked `Receive`, joins the
worker, then releases allocator resources. There is one deliberate
compatibility exception: the game's texture-renderer path uses graph-level
`IMediaControl::Stop` as a per-frame presentation tick. While an ASF reader is
attached, that graph call updates the graph's logical state without stopping
its filters; the subsequent Run performs the pause/run hand-off. The graph
tracks attached ASF readers for this exception, and `RemoveFilter` decrements
the count only after a successful removal so a detached reader cannot leave
ordinary graph Stop calls permanently bypassed. The RenderFile helper connects the source to an
input pin already present in the graph; it is not a general DirectShow
Intelligent Connect implementation. The game-specific source/renderer setup
remains the primary target.


Graph collection operations also protect against re-entrant guest COM callbacks.
`RemoveFilter` holds temporary references to both the graph and target filter,
and marks that filter as being removed until every exit path has completed.
A nested removal of the same filter is rejected with
`VFW_E_WRONG_STATE`; removals of other filters are revalidated against the
current collection after callbacks return. Final graph `Release` uses a
private teardown reference, stops child filters before disconnecting their
pins, and calls `JoinFilterGraph(NULL)` only after disconnect succeeds.
Graph mutation and state-transition calls are rejected while teardown is in
progress. If Stop, pin disconnection, or JoinFilterGraph fails, the graph and
its owning filter references are deliberately retained instead of freeing
objects that may still be reachable from a streaming callback. A subsequent
AddRef/Release cycle can retry the deferred teardown; if no owner retries, this
safety fallback can leave a retained graph allocation.


## Game-specific COM compatibility cross-check

A comparison with [Life Support for The Movies](https://github.com/KillerPixelCrew/lifesupport-TheMovies)
identified a game-specific interface quirk that a generic DirectShow implementation
would miss. Its reverse-engineering notes identify `MoviePlayer_Setup` and record
this sequence:

1. Create `CLSID_FilterGraph`.
2. Create `CLSID_WMAsfReader`.
3. Render/connect the source output to the game's texture renderer.
4. Query the graph for `IMediaControl`.
5. Query the graph for `IMediaSeeking`.
6. Query the graph for `IMediaPosition`.
7. Query the graph for `IBasicAudio`.

The same notes report that the game stores the result of the
`IID_IMediaSeeking` query but later calls `IMediaPosition` methods through
that pointer. The Windows compatibility implementation therefore returns its
`IMediaPosition` interface pointer for both IIDs. The Android graph now does
the same: both queries resolve to the graph's existing
`FilterGraphMediaPosition` interface and share the controlling graph
reference count. This is an intentional game-ABI compatibility alias, **not**
a claim that the two DirectShow interfaces are interchangeable in general.

The reference implementation also delegates graph `get_CurrentPosition` and
`get_Duration` to the source filter's `IMediaSeeking`. The game polls
current position against duration to decide when an intro has finished. A
graph-side position cache that remains at zero would therefore keep reporting
an unfinished video even if decoding and sample delivery reached EOF. The
Android reader now publishes its file-timeline position atomically after each
accepted frame, sets it to the known duration at EOF, and resets it when a
new playback/loop starts. Graph `get_CurrentPosition` reads that value from
the active ASF reader. This provides progress reporting only; seeking and
rate changes remain unsupported.rate changes remain unsupported.

### Follow-up audit: playback lifecycle against Life Support

A source-level comparison of the Android reader with the later Life Support
playback fixes found:

- Pause/resume is represented in the Android worker: pausing an active reader
  sets its paused/running flags and Run resumes the same worker. This matches
  the functional requirement described by Life Support for menu navigation.
- The Android reader loops only paths matching frontend_loop / loop.wmv.
  At loop EOF it resets the published position, reopens the decoder, sends a
  new segment, and does not send EOS or EC_COMPLETE. Non-loop files publish
  duration, then send downstream EOS and graph EC_COMPLETE. This is
  consistent with the intended distinction between the menu loop and intros.
- A lifecycle detail remains to verify: when a non-loop worker reaches EOF,
  it clears the atomic running flag and sets worker_done, but does not
  itself change the reader's FILTER_STATE field from Running. A later
  Stop resets that state; a later Run joins/reopens a completed worker.
  The game may not care, but this is not proven. Runtime traces should record
  GetState, Stop, Pause, and Run around EOF and menu transition before
  changing this behaviour.
- The Life Support project reports a real teardown hang inside downstream
  IMemInputPin::Receive, solved there with BeginFlush before destroying
  its playback backend. The Android reader already has flush-generation
  tracking and uses downstream BeginFlush/EndFlush around stream changes;
  do not transplant the Life Support fix mechanically. If Android teardown
  hangs, capture whether the worker is blocked in Receive, GetBuffer, or
  another callback first.

These are source-level findings and runtime hypotheses, not device validation.


There is a second, separate completion gate in the game's playback controller.
The Life Support Ghidra notes identify its field at offset `0x1C` as a game-side
state (`3 = setup`, `1 = playing`) and report that `IsVideoFinished` only
uses the position/duration comparison in controller states 1 and 2; other
states are treated as finished. This is **not** the DirectShow graph's
`FILTER_STATE` (Stopped/Paused/Running). Do not map the two state machines
onto each other. If the position fixes do not advance the intro, the runtime
trace must include the game's controller-state transitions as well as graph
state and media position.

One limitation needs explicit attention: Android
`IMediaPosition::put_CurrentPosition` currently validates and stores the
requested value in the graph cache, but does not seek the FFmpeg decoder.
The next `get_CurrentPosition` reads the reader's actual position and can
overwrite that cached value. Treat this method as a compatibility stub, not
as working seeking. At runtime, record every call and argument; a nonzero
request or a reset after playback has begun would require a real seek/reopen
implementation before claiming correct replay behaviour.

The cross-check also records two behaviours already represented in this
Android graph: the texture renderer can deliver `EC_COMPLETE` through the
`IFilterGraph` pointer's slot corresponding to `AddFilter`, and the game
uses graph-level `Stop` / `Run` calls as a per-frame presentation sequence
while an ASF reader is attached. Keep these narrowly scoped compatibility
rules; do not generalize them to unrelated filters or graphs.

The reference project is a 32-bit Windows ASI mod (also used with Wine), not
an Android port. Its MinHook, WASAPI and Windows-specific libmpv integration
are not directly reusable in this Android runtime. Its value here is the
game-specific reverse engineering and the independently verified call
sequence. Its README reports launch through the menu and transition into the
game; its engineering notes explicitly say extended simulation/campaign play
has not yet been exercised.

This comparison is source-level evidence only. The Android runtime still
requires a device trace to confirm that the same executable path reaches these
calls and that the alias gets past the current Lionhead splash.


## FFmpeg build

Use [the Android build guide](ANDROID-BUILD.md#7-build-the-minimal-android-wmv-decoder)
to build the pinned FFmpeg 9.1 subset. It enables the ASF demuxer, WMV1/2/3 and
VC-1 decoders, the VC-1 parser, and the local-file protocol. FFmpeg is linked
as shared LGPL libraries. No network protocols, command-line programs, or
static FFmpeg libraries are enabled.

## Deliberate limitations

- Video only. WMV audio is not decoded or delivered.
- No general Intelligent Connect, transform filters, renderer insertion, or
  multi-pin graphs.
- No seeking, rate changes, frame stepping, alternate RGB subtypes, or dynamic
  media-type renegotiation.
- No Windows Media profile manager, sync reader, or writer functionality.
- The reader assumes the game's texture renderer accepts RGB24 and supports
  the allocator/sample path used here.
- The loop restart is filename-based and currently recognises
  `frontend_loop` or `loop.wmv`.

These constraints should be extended only when confirmed by the game's lifted
call sites or runtime traces, not by adding speculative DirectShow stubs.

## Validation still required

Before calling the media path functional, verify on an ARM64 Android device:

1. FFmpeg's shared libraries load and the linker resolves their SONAMEs.
2. `CLSID_WMAsfReader` is instantiated and `IFileSourceFilter::Load` opens
   the three WMV files from the installed game-data mount.
3. The game's renderer accepts the advertised media type and allocator
   properties.
4. Frames arrive at the expected dimensions, orientation, cadence, and
   colours; no stale frame or blank texture remains after completion.
5. Stopped-to-paused produces a first frame for the renderer; Run resumes the
   worker with the paused time excluded from presentation timing. Verify that
   `BeginFlush` cancels a pending `Receive`, that `EndFlush` resumes streaming,
   and that Stop joins the worker without deadlock. Verify that
   `frontend_intro.wmv` transitions to the loop without destroying the reader
   or renderer, and that repeated pause/stop/run calls do not deadlock.
6. The main menu accepts input and starts a new game; then verify an actual
   gameplay session, not only the frontend.

No APK build or device validation has been run for this implementation yet.
