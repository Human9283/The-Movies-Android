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
   zero for the new segment. The first sample of each segment is marked
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
   loop-reopen, segment-negotiation, and downstream-delivery failures report
   `EC_ERRORABORT` with the corresponding HRESULT. A Stop/flush cancellation
   does not masquerade as a media failure.

The graph also routes `IGraphBuilder::RenderFile` and
`IGraphBuilder::AddSourceFilter` through this reader. Graph state changes run
through the filters downstream-first so the renderer is ready before the
source starts pushing samples. `IMediaControl::GetState` polls child filter
states using the caller's timeout, including nonblocking zero-timeout polls,
`VFW_S_STATE_INTERMEDIATE`, and `VFW_S_CANT_CUE`. The source forwards
`BeginFlush`/`EndFlush` to the downstream pin; `Stop` signals the worker,
flushes the peer to release a blocked `Receive`, joins the worker, then
releases allocator resources. The RenderFile helper connects the source to an
input pin already present in the graph; it is not a general DirectShow
Intelligent Connect implementation. The game-specific source/renderer setup
remains the primary target.

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
