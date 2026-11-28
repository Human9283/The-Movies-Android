# Android porting research and architecture review

Date: 2026-11-28

## Executive result

The current port architecture is consistent with established native Android PC-game
ports. It is not a Wine/Winlator project and should not be redirected toward one.
The most relevant public comparison found is the Android static recompilation of
Mirror's Edge: PC x86 code is lifted ahead of time, a Win32 compatibility runtime
is supplied, Direct3D 9 is translated through DXVK to Vulkan, and SDL provides the
Android application/input layer.

The important correction is that
[Life Support for The Movies](https://github.com/KillerPixelCrew/lifesupport-TheMovies)
is **not an Android port**. It is a 32-bit Windows ASI mod that also runs with
Linux/Wine. It is nevertheless unusually valuable because it targets the same
game and documents reverse-engineered DirectShow call sequences and quirks.

The comparison produced one concrete Android source fix: the game queries
`IID_IMediaSeeking` on the graph but subsequently calls `IMediaPosition`
methods through the returned pointer. The graph previously rejected this IID
and would call `hle_fatal`. It now returns the existing graph
`IMediaPosition` interface for both IIDs. See the code comment in
`src/host/tm_dshow.c` and the corresponding section in
`ANDROID-DIRECTSHOW.md`.

No APK/native build, CI run, GitHub Actions workflow, or device test was run.

## What other projects actually do

### Mirror's Edge — closest architectural comparison

[RunnerRecompiled](https://github.com/singlemate/RunnerRecompiled) is a native
Android ARM64 static recompilation of the Windows PC version of Mirror's Edge.
Its README describes:

- translating the game's x86 code to C ahead of time on a PC;
- compiling the result for Android ARM64;
- supplying a runtime for Windows services and game-specific fixes;
- translating D3D9 through DXVK to Vulkan;
- SDL-based Android activity and touch-to-mouse mapping;
- native game audio and Bink video paths.

Its README reports real-device menu and chapter testing, including a
Snapdragon 8 Gen 3 / Adreno 750 device. This is the closest match to the
The Movies Android architecture: PC x86 static recompilation, a compatibility
runtime, D3D9/DXVK/Vulkan, and an Android shell.

### Stronghold — PC x86 plus Android Win32 compatibility

[pc_decomp_strhld](https://github.com/phys-winner/pc_decomp_strhld) statically
recompiles PC x86 executables with pcrecomp, then supplies an Android Win32
compatibility layer. Its native host replaces platform-specific facilities:
graphics are sent to OpenGL ES, audio uses Android AAudio/OpenSL ES, and input
is adapted to touch, mouse, keyboard and controllers. Its build scripts pin
pcrecomp and toolchain versions, generate C from the user's own executables,
compile native libraries with the NDK, and package the APK.

This demonstrates that an Android Win32 HLE layer is a normal approach for
static PC recompilation. It also demonstrates the value of implementing the
specific host APIs a game uses rather than recreating an entire Windows system.

### Mewgenics — a different, 64-bit example

[mewgenics-android](https://github.com/zwaetschge/mewgenics-android) uses
pcrecomp to lift a Win64 executable to C, then compiles for Android with its own
Win64 runtime, SDL3 and OpenGL ES. It does not use Wine or Box64 at runtime.
Its runtime surface includes PE loading, TEB/PEB/TLS, kernel32 and Steam API
stubs, while graphics calls are adapted to GLES.

This is useful as a reference for build reproducibility and runtime boundaries,
but its Win64 ABI and GLES renderer are not a drop-in replacement for this
project's x86 Win32 HLE and D3D9/DXVK renderer.

### Life Support for The Movies — same game, different target

[Life Support README](https://github.com/KillerPixelCrew/lifesupport-TheMovies)
describes a 32-bit Windows ASI mod built with MinGW i686. It hooks Windows
APIs with MinHook, replaces the DirectShow graph in-process, uses libmpv for
FMV playback and WASAPI audio, and uses FFmpeg/libavformat for movie export.
It is not a portable Android implementation.

Its [STATUS engineering notes](https://github.com/KillerPixelCrew/lifesupport-TheMovies/blob/main/STATUS.md)
are still a strong game-specific reference. They identify the startup
`MoviePlayer_Setup` call sequence, the `IMediaSeeking` /
`IMediaPosition` ABI mismatch, the renderer's unusual `EC_COMPLETE`
notification route, and the game's per-frame Stop/Run pattern.

The README describes launch through the main menu, menu navigation,
transition into the in-game mode, and movie export as verified. The STATUS
notes distinguish that from extended gameplay: starting a studio, running
the simulation and campaign were not yet exercised. These statements should
not be conflated into a claim that the entire game was completed and tested.

## Comparison with this repository

| Concern | The Movies Android | Mirror's Edge Android | Stronghold Android | Life Support |
|---|---|---|---|---|
| Original target | PC x86 | PC x86 | PC x86 | PC x86 |
| Code execution | Static lift to C, ARM64 | Static lift to C, ARM64 | Static lift to C, ARM64 | Original x86 process |
| Windows compatibility | Win32 HLE | Win32 runtime | Win32 compatibility | Real Windows APIs |
| Graphics | D3D9 → DXVK Native → Vulkan | D3D9 → DXVK → Vulkan | Native game rasterizers → GLES | Game's D3D9 / optional wrappers |
| Audio | Android host path under development | Native runtime audio | AAudio / OpenSL ES | WASAPI via libmpv |
| Video | FFmpeg ASF/WMV video-only reader | Recompiled Bink path | Original Miles/Bink paths | libmpv for FMV |
| App shell | SDL2 + Android Gradle | SDL + Android app | Android launcher + native host | Windows ASI loader |
| Relevant proof | D3D9 probe passed; full game/menu not confirmed | Real-device chapter tests reported | Android release with scope caveats | The Movies menu path reported verified |

## Decisions for this project

1. **Keep the overall architecture.** pcrecomp + ARM64 Win32 HLE + DXVK Native
   D3D9/Vulkan + SDL2/Android is a defensible design and has close precedents.
   Replacing it with Wine/Winlator would change the product into a runtime
   compatibility environment and discard the current static recompilation work.

2. **Do not port Life Support's Windows plumbing.** MinHook, WASAPI and its
   Windows-specific libmpv build are not suitable Android dependencies as-is.
   Reuse its game-specific reverse-engineering findings, not its OS integration.

3. **Keep the media implementation narrow.** The Android reader is explicitly
   video-only and supports the startup/frontend WMV path. Do not add a generic
   DirectShow transform graph, Windows Media profile manager, SyncReader or
   writer merely to mimic Windows. Extend the surface only when a lifted
   call-site or runtime trace demonstrates that the game needs it.

4. **Do not replace FFmpeg with libmpv by default.** FFmpeg is already built for
   Android and used by the current decoder. libmpv could be evaluated if the
   game requires tightly synchronized audio/video playback, but it would add
   an Android audio/render integration project. It is not a free drop-in
   replacement for the Life Support backend.

5. **Treat the IMediaSeeking alias as a game-specific ABI accommodation.**
   It is supported by the independent Ghidra notes and has now been added to
   the Android graph. Do not advertise it as standards-compliant DirectShow.

6. **Prioritize the actual blocker.** The next device trace should establish
   whether startup reaches `MoviePlayer_Setup`, whether the sequence of graph
   creation / reader creation / Render / QI now completes, whether the renderer
   accepts the RGB24 allocator samples, and whether `EC_COMPLETE` advances
   from the Lionhead/intro sequence into the frontend. Then check menu input
   and a new-game transition. This should be a staged test, not a speculative
   rewrite.

## Next validation checklist

These are the next required runtime checks; they have not been performed by
this source-level review:

- Confirm the exact game executable revision/hash used by the Android port is
  the same revision assumed by the external Ghidra findings.
- Capture the actual COM sequence and HRESULTs for FilterGraph, WMAsfReader,
  Render, IMediaControl, IMediaSeeking, IMediaPosition and IBasicAudio.
- Confirm the new IMediaSeeking query returns the same graph-owned interface
  pointer as IMediaPosition and that each AddRef/Release balances.
- Confirm the reader opens `atvi_logo.wmv`, `frontend_intro.wmv` and
  `frontend_loop.wmv` from the installed game-data mount.
- Confirm the renderer negotiates RGB24, receives the first paused/preroll
  frame, resumes at Run, and releases blocked Receive calls on flush/Stop.
- Confirm end-of-stream / EC_COMPLETE behavior, intro-to-loop transition,
  menu navigation, and transition into a new game.
- Only after those checks, investigate audio synchronization, menu-loop
  teardown/re-entry, and gameplay-specific media paths.

## Source references

- [Android port build and dependency graph](ANDROID-BUILD.md)
- [Android DirectShow implementation and validation limits](ANDROID-DIRECTSHOW.md)
- [RunnerRecompiled (Mirror's Edge)](https://github.com/singlemate/RunnerRecompiled)
- [Stronghold Android static recompilation](https://github.com/phys-winner/pc_decomp_strhld)
- [Mewgenics Android static recompilation](https://github.com/zwaetschge/mewgenics-android)
- [Life Support for The Movies README](https://github.com/KillerPixelCrew/lifesupport-TheMovies)
- [Life Support reverse-engineering notes](https://github.com/KillerPixelCrew/lifesupport-TheMovies/blob/main/STATUS.md)
