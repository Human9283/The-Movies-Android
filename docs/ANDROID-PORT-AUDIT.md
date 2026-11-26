# Android ARM64 port: implementation audit and next steps

Status: historical source audit of public repository `main` at
`a6a3ba601ef95d5922e6b66b99653d83121255e0`. Later Android diagnostic work
is recorded in the dated updates below and in [ANDROID-FEASIBILITY.md](ANDROID-FEASIBILITY.md).
This is not a claim that Android currently runs the game.

## Executive finding

The existing runtime is a 64-bit host process that executes a 32-bit
Windows guest through lifted C and win32hle. That architecture is not
inherently x86-64-only: the CMake top-level check requires a 64-bit host,
and the generated guest code uses 32-bit guest addresses. An AArch64 host
is therefore a plausible target.

The game runtime in this repository is still a Linux desktop executable,
not an Android game application. Two separate diagnostic-only Android apps
now exist on the active feature branch: `android-probe/` checks Vulkan
capabilities and device/swapchain creation, while `android-d3d9-probe/`
builds an Android-adapted DXVK Native D3D9 library and tests its SDL2/Vulkan
path. Neither hosts the game or includes lifted game code. The full Android
game port remains blocked by application lifecycle/window integration,
build-system assumptions, and the unverified compatibility of the D3D9
backend with the actual phone.

Do not start by changing the lifted game code or by assuming that DXVK
3.1.1 will work on every Android GPU.

## Findings from source

### Important correction: aspirational CMake comment

The current `CMakeLists.txt` header says the same file builds for Linux
x86-64, Linux arm64 and Android arm64-v8a with an NDK toolchain. That
statement is not borne out by the actual build logic below it. The file
unconditionally requires desktop `PkgConfig`, SDL2 and SDL2_ttf, uses
desktop library discovery and defines only the `tm_host` executable.
There is no Android shared-library target or JNI/SDLActivity integration.
The Android feasibility document also explicitly marks the NDK build as
"not done". Treat the header line as a target/intent, not evidence of a
working Android build. Linux AArch64 is likewise not yet measured in the
project's run records.

### 1. Host architecture

- `CMakeLists.txt` rejects only hosts where `CMAKE_SIZEOF_VOID_P != 8`.
- The guest ABI is explicitly 32-bit: the D3D9 proxy objects are 16-byte
  guest objects, guest addresses are `uint32_t`, and guest pointers are
  translated with `hle_ptr()`.
- `tm_d3d9_dxvk.cpp` converts guest structures to host DXVK structures
  and has compile-time layout assertions for the D3D9 POD structures.
- This is compatible in principle with a 64-bit AArch64 host, but it is
  not proof: generated C, win32hle, function-pointer casts, atomics,
  inline assembly, and memory reservation behavior still need an
  AArch64 compile and runtime test.

### 2. The current executable is not an Android app

- The Linux command-line entry point is now isolated in `src/host/tm_host_main.c`; the reusable bootstrap is `tm_host_run()` in `src/host/tm_host.c`. The Android caller/JNI layer does not exist yet.
- The game directory is passed as a command-line argument and defaults
  to relative host paths such as `tm_user` and `tm_system`.
- The game itself still has no Android game Gradle target, linked native
  runtime, activity/JNI game integration, Android data importer, or game APK
  packaging. A separate
  `android-probe/` and `android-d3d9-probe/` Gradle projects now exist on
  the active feature branch. Both are standalone diagnostics and must not
  be mistaken for game packaging.
- The runtime currently expects the game directory to be available as a
  normal filesystem path. Android must select a private app data directory
  or a user-imported directory and pass that path to the native runtime.
- The current Linux window is owned by win32hle/SDL. The Android lifecycle
  must create and destroy the SDL window/surface in response to activity
  and native-window lifecycle events; it cannot be treated as a persistent
  desktop window.

### 3. Build-system blockers

The active CMake implementation still assumes a desktop build for its configured target:

- The Linux target now uses a reusable `TM_RUNTIME_SOURCES` list plus the separate `tm_host_main.c` adapter. No Android target or Linux AArch64 validation target is implemented yet.
- `find_package(PkgConfig REQUIRED)` and
  `pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf)`
  require desktop pkg-config modules. Android should use SDL2's Android
  CMake integration or imported NDK-built SDL targets, not host pkg-config.
- `find_package(Threads REQUIRED)` and the host's system library names
  need validation under the Android NDK toolchain.
- The tests create desktop executables and set dummy SDL drivers. They
  should remain host-only; do not attempt to build the whole CTest suite
  into the APK.
- DXVK is currently discovered through a pre-installed Linux prefix using
  `find_library` and `include/dxvk`. Android needs a separate AArch64
  cross-build and a Bionic-compatible library set.
- The CMake file currently links `m`, SDL2 and SDL2_ttf as if the
  target were a desktop executable. The Android target needs an Android
  shared library entry point and the appropriate Android system libraries.

Recommended structure: keep the proven Linux host build unchanged; add
an Android-specific CMake entry point or options and a small Android
application directory. Avoid a large conditional rewrite of the existing
desktop target.

### 4. DXVK and Vulkan are a separate gate

The current Android feasibility document records the first S24 Ultra
report: Adreno 750 exposes Vulkan 1.3.128 and the basic WSI path, but lacks
three extensions required by the pinned DXVK 3.1.1 build. The project is
therefore evaluating DXVK 2.4.1 as a lower-requirement candidate; its
baseline still needs a real Android device/swapchain test and a separate
DXVK Native build/runtime test.

The game itself uses DXT1/DXT3 textures. The fact that the game uses
D3D9 fixed-function rendering does not remove DXVK's Vulkan device
requirements.

Before investing in full APK integration, build the smallest Android
Vulkan/SDL proof of concept for the target device and capture:
- device name and driver version;
- Vulkan API version;
- required feature and extension availability;
- successful SDL Android Vulkan surface creation;
- successful DXVK D3D9 device creation and one presented frame.

A desktop lavapipe pass does not establish any of these Android facts.
The first S24 Ultra report is recorded in `docs/ANDROID-FEASIBILITY.md`;
the next APK adds a real Vulkan device and swapchain smoke test for the
DXVK 2.4.1 baseline.

## Android NDK compile check added (26 November 2026)

The Android D3D9 probe's CMake build now compiles the repository's portable
host C translation units with the Android NDK for `arm64-v8a`, including the
runtime bootstrap, the new Android SDL entry point (`tm_host_android.c`),
loader, Win32 shims, VFS, input, media and D3D9 host modules.
The existing `tm_d3d9_dxvk.cpp` bridge is also compiled against the pinned
Android DXVK headers. These are object-only checks: the diagnostic APK still
does not link the game runtime, and no generated `recomp_*.c` files are
available in the public repository. The check can expose Bionic/ARM64 compile
failures before a full Android link is attempted, but does not validate guest
memory, PE execution, game startup, or rendering of The Movies itself.

## Recommended implementation sequence

### A0 — preserve the known-good desktop path
- Keep `main` unchanged.
- Keep all new Android work on a feature branch.
- Continue to run the existing Linux tests and DXVK smoke tests after
  shared-code changes.

### A1 — make the host build split explicit
- Introduce a reusable host/runtime source list.
- Keep the current `tm_host` executable and its CLI unchanged on Linux.
- Add an Android shared-library target only when an Android toolchain is
  selected.
- Gate desktop-only tests and Xvfb tests behind non-Android conditions.
- Replace desktop pkg-config discovery with target-specific SDL2
  dependency handling.

### A2 — prove AArch64 compilation before Android packaging
- Cross-compile the runtime and lifted C for `aarch64-linux-gnu` on Linux.
- Run non-graphics unit tests under an AArch64 environment/emulator where
  practical.
- Verify the guest-memory reservation and pointer translation on AArch64.
- Resolve compiler/runtime issues before adding Android lifecycle code.

### A3 — create the Android SDL shell
- Add a minimal Android application with SDL's Android activity glue.
- Add a JNI/native entry point; do not call desktop `main()` directly.
- Receive the app-private game-data directory and user-data directory
  from the activity.
- Handle pause/resume, surface loss/recreation, and process termination.
- Keep game data outside the APK; provide an import/selection flow and
  validate required files before launch.

### A4 — validate Vulkan and DXVK on the actual device
- Build SDL2 and the selected DXVK revision for Android ARM64.
- Test Vulkan surface creation independently of the game.
- Test DXVK D3D9 device creation and presentation.
- Record actual device capabilities; do not infer from GPU brand alone.

### A5 — bring up the original game
- Start with the original executable and user-supplied game data.
- Test menu rendering, touch-to-mouse mapping, lifecycle recovery and
  resolution changes.
- Then continue into studio simulation, video playback, audio and
  save/load. These are separate milestones and must not be marked complete
  from a menu-only run.

## Scope and legal/data boundary

The public repository must contain only project code, documentation,
tests and properly attributed third-party notices. Do not commit ISO
images, extracted game files, the original executable, lifted proprietary
game code, or copyrighted game assets. Keep all such data local or in the
user's private storage.

## Immediate next action

The next code change should first reconcile the misleading platform claim
in the CMake header and then separate desktop-only dependencies from
portable runtime sources. Before changing the target graph, obtain a clean
baseline build and CTest result from the existing Linux environment.
Without that baseline, a GitHub-only edit cannot establish that the
working desktop build was preserved. The NDK target, SDL Android glue and
APK packaging should remain separate, subsequent changes.

## Follow-up source inspection: concrete host-side Android work

The source-level inspection narrows the first refactor. The immediate problem is not the lifted game code; it is that desktop process setup, diagnostics and window ownership are concentrated in the host entry point.

### 6. src/host/tm_host.c is both runtime bootstrap and desktop process

The file currently combines:
- CLI parsing (main, argv, relative defaults for user/system paths);
- process-level signal handlers (sigaction, sigaltstack, alarm);
- POSIX termination and diagnostics (_exit, unistd.h, stderr);
- SDL window creation through hle_screen_open();
- registration of all game-specific Win32 shims;
- guest image mapping, import binding and the call into the lifted entry point.

This is a strong reason not to make Android call main(). The useful extraction boundary is a parameterised runtime-start function that accepts explicit game, user and system paths plus runtime options, while leaving argument parsing and desktop signal/watchdog policy in the Linux executable. An Android JNI entry point can then call the same bootstrap with app-owned paths. This is a design recommendation, not a completed refactor.

### 7. Thread ID calls are a portability check, not yet a proven blocker

src/host/tm_itrace.c and src/host/tm_dinput8.c include sys/syscall.h and unistd.h, then use syscall(SYS_gettid) for diagnostic thread IDs. Android's Bionic exposes Linux system-call interfaces, but this exact source must still be compiled with the selected NDK/API level. If it fails or is undesirable for app code, replace only the diagnostic ID provider with a small platform abstraction (or SDL's thread ID API); do not change guest thread semantics as part of a logging cleanup.

### 8. Filesystem paths need an Android-owned root

src/host/tm_shell.c uses mkdir() and host paths to create the guest D: folders; tm_host.c passes those roots into win32hle. This is already reasonably isolated behind tm_shell_init() and tm_sysroot_init(). The Android shell should supply paths under the app's private files directory and must not rely on the process working directory. The current CLI defaults are unsuitable as Android defaults, but the folder mapping does not need to be redesigned from scratch.

### 9. Input is already routed through SDL, but touch behavior is unproven

 tm_dinput8.c consumes win32hle's shared keyboard/mouse state and does not itself read Linux input devices. This is a useful separation: SDL's Android backend can be the source of input events. However, the current game requests exclusive foreground mouse input, and the implementation uses relative mouse deltas. Touch-to-pointer mapping, virtual cursor behavior, gestures, keyboard visibility and lifecycle focus changes remain unimplemented/unverified product work. Do not count SDL compiling for Android as proof that the game is playable by touch.

### Resulting order of work

1. Establish and record the Linux baseline from the exact current branch.
2. Extract a runtime bootstrap API from tm_host.c without changing its Linux CLI behavior; keep signal/watchdog handling in the desktop wrapper.
3. Add host-side regression coverage for the extracted bootstrap and verify the existing CTest and game boundary tests.
4. Cross-compile the shared runtime for Linux AArch64.
5. Only then add the Android JNI/SDL activity wrapper and app-private path handling.

No C/CMake refactor was committed in this follow-up: without a local build, the GitHub-only environment cannot validate the changes against the 10-million-line lifted translation unit or the existing DXVK integration.

## Runtime entry-point extraction (first code step)

The first host refactor is now present on the feature branch:

- `src/host/tm_host.h` defines `tm_host_options` and declares `tm_host_run()`.
- `src/host/tm_host.c` now owns the shared bootstrap in `tm_host_run(options)`.
- The existing desktop `main()` remains the CLI adapter: it parses the same options and forwards them to `tm_host_run()`.
- `main()` is excluded when `__ANDROID__` is defined, so an eventual Android native entry point can link the shared bootstrap without a duplicate process entry point.

This is an interface extraction only. It does not yet create an Android JNI bridge, Gradle project, SDL Android activity, or Android CMake target. The runtime still installs the existing POSIX crash handlers and uses the existing SDL/window path; those behaviors need explicit Android lifecycle review before an APK can be built.

The existing Linux command-line options are preserved by forwarding their values through `tm_host_options`. No build or test result is claimed for this commit because the local PCRECOMP checkout, lifted C tree and DXVK prefix are not available in this environment. The next validation must be a clean Linux configure/build/CTest on the user's machine before proceeding to platform-specific changes.

### Reusable CMake source group

The desktop target's source list has also been collected into `TM_RUNTIME_SOURCES` and `tm_host` now consumes that list. This is a mechanical build-graph preparation for a future Android shared-library target; it does not change the current target type, dependencies, compiler flags or test graph. The Android target is intentionally not declared until SDL2/NDK/DXVK dependency handling is specified and can be validated.


## Runtime/CLI source split (follow-up implementation)

The previous entry-point extraction left the Linux `main()` in
`src/host/tm_host.c`, guarded by `#ifndef __ANDROID__`. That still made
the runtime translation unit own the desktop command-line adapter. This
has now been corrected:

- `src/host/tm_host.c` contains the shared `tm_host_run()` bootstrap and
  runtime helpers; it no longer defines `main()`.
- `src/host/tm_host_main.c` contains the Linux argument parser and calls
  `tm_host_run()`.
- The desktop CMake target explicitly adds `tm_host_main.c` alongside
  `TM_RUNTIME_SOURCES`.

This is a mechanical source-boundary change. It does not add an Android
target, JNI, Gradle, SDL Android activity glue, or an Android DXVK build.
The desktop build and tests have not been run in this environment. The
next user-side validation remains the existing Linux configure/build/CTest
and the game boundary test before further changes to the shared runtime.

### Remaining constraints in the extracted runtime API

The current `tm_host_run()` seam is not yet a complete Android lifecycle
API:

- It still installs process-wide POSIX signal handlers and an optional
  `SIGALRM` watchdog.
- The WinMain diagnostic stop path calls `_exit(0)`; it is intended for
  desktop diagnostics, not normal Android app operation.
- The default user/system paths are relative when callers pass NULL.
  Android must always pass explicit app-private absolute paths.
- Runtime state is held in file-static globals, so repeated start/stop
  within one process is not currently supported or proven.
- SDL window/surface creation is still initiated by the existing D3D9
  backend path; Android pause/resume and native-window recreation have
  not been integrated.

The Android bridge must therefore be a one-shot launch initially, pass
explicit paths, leave diagnostic stop/watchdog options disabled, and
coordinate SDL lifecycle before it can be considered usable.


## Guest-memory dependency: AArch64 gate

The pinned build instructions use pcrecomp revision
`7c07a0b42059b6dc4313d9b4ebded6f7a72db8f3`. Its `runtime/win32hle/guest_mem.c`
describes the 64-bit "based" mode as one 4 GiB guest-address reservation
at `g_mem_base`; guest virtual addresses are offsets into that reservation.
The dependency also contains a compatibility definition for
`MAP_FIXED_NOREPLACE`. This is a Linux virtual-memory implementation detail,
not an Android guarantee.

Consequences for the port:

- The first AArch64 test must use the same pinned pcrecomp revision as the
  game build, not a newer checkout with potentially different memory code.
- Verify that the 4 GiB reservation succeeds under the target Android
  process address-space layout and that PE image mappings/relocations still
  work. A successful C compile does not prove this.
- Record the actual reservation base and confirm that no guest VA is
  accidentally truncated or converted through a 32-bit host pointer.
- Keep the normal variable-base mode for the first experiment. Do not enable
  a fixed host base merely as a performance optimization until the Android
  address-space layout has been measured.
- Run the existing PE loader and guest-memory tests on AArch64 before
  attempting DXVK or the full game.

This is now a separate explicit porting gate between "AArch64 compiles" and
"Android graphics works". It is not yet tested on Android hardware.


## Update after the runtime/CLI split and external research (2026-11-26)

The implementation branch now has a concrete source split:

- `src/host/tm_host.c` exports `tm_host_run(const tm_host_options *)`
  and no longer defines the process `main()`.
- `src/host/tm_host_main.c` owns the Linux CLI parser and calls
  `tm_host_run()`.
- `CMakeLists.txt` builds the Linux executable from
  `tm_host_main.c` plus the reusable `TM_RUNTIME_SOURCES` list.
- The source-list comment was corrected to describe this actual state.

This is only the first boundary. The runtime still has process-global
state, POSIX signal/watchdog setup and an `_exit()` diagnostic path; an
Android entry point must use explicit app-private paths and must not
pretend that runtime pause/resume or repeated starts are implemented.

External research was added to `docs/ANDROID-FEASIBILITY.md`:
NDK r30 / SDK 36 are the proposed toolchain baseline; SDL's current Android
template uses Gradle, `SDLActivity`, JNI and a native shared library;
upstream DXVK explicitly does not provide first-party Android/mobile-driver
support. The DXVK Android feasibility gate must be measured on the actual
phone, not inferred from Vulkan API version alone.

The repository README's opening status was also corrected: the Linux
runtime has progressed beyond WinMain to the logo, main menu and 3D studio
lot. That progress must not be confused with Android support.

No Android target, JNI bridge or APK has been added in this update. No
local build/test result is claimed.


## Android probe implementation status (2026-11-26)

A separate `android-probe/` project has been added. It is intentionally
not wired into the game CMake graph and packages no game files.

- Android `NativeActivity` + NDK `android_native_app_glue`.
- ARM64-only Gradle application, API 26 minimum, compile SDK 36.
- Native Vulkan loader and Android-surface creation.
- Device/driver/API identification, graphics and presentation queue checks,
  required DXVK v3.1.1 extension checks, mandatory core 1.1/1.2/1.3
  feature checks, selected mandatory extension feature checks, and the
  256-byte push-constant limit.
- Separate GitHub Actions workflow intended to compile the debug APK.

The requirements are read from the project's exact DXVK pin
(`b1a1c99ab52b687cf950d62c88bc2fa316b41663`), not from a newer DXVK
release. The probe's `PASS` means capability enumeration passed; it does
not prove a Bionic build of DXVK, swapchain creation, or a presented frame.

**Validation status:** the standalone probe has now completed a real
Android ARM64 CI build with Gradle 8.11.1, Android SDK 36, NDK
28.2.13676358 and CMake 3.22.1. GitHub Actions run
[45733757467](https://github.com/Human9283/The-Movies-Android/actions/runs/45733757467)
completed successfully and uploaded the
`the-movies-vulkan-probe-arm64-debug` artifact. The APK archive was
downloaded and inspected: it contains `lib/arm64-v8a/libmain.so`, which
is an AArch64 Android shared object targeting API 26.

This verifies that the NativeActivity/NDK Vulkan probe compiles and packages.
It does **not** verify Vulkan capability results, a rendered frame, DXVK
Bionic compatibility, or the game runtime. The first hardware run is still
required on the target phone. See
[`android-probe/README.md`](../android-probe/README.md).


## Status update: standalone Vulkan diagnostic APK (2026-11-26)

The audit above describes the game runtime, not the separate probe. Since
the audit was first written, the feature branch
`audit/android-arm64-next-steps` gained an `android-probe/` project.

- GitHub Actions run
  [45734393656](https://github.com/Human9283/The-Movies-Android/actions/runs/45734393656)
  completed successfully and uploaded
  `the-movies-vulkan-probe-arm64-debug`.
- The APK targets Android `arm64-v8a`, uses a Java Activity plus JNI to
  provide an Android Surface to native Vulkan code, and displays the
  capability report on screen with a clipboard-copy button.
- The native check queries loader/device API versions, GPU and driver
  properties, graphics/present queues, the DXVK 3.1.1 feature/extension
  gate, and Android surface capabilities.
- The probe does not create a Vulkan logical device, does not load DXVK,
  and does not launch The Movies. A successful APK build is not a device
  compatibility result. A physical-device report is still required.

This update supersedes the audit's earlier statements that no Android
Gradle project or APK exists, but only for the diagnostic app. All findings
about the absence of an Android game runtime remain valid.


## Updated physical-device graphics evidence (2026-11-26)

The user successfully installed and ran probe 0.3 on a Samsung Galaxy S24 Ultra (Adreno 750). The report confirms:

- Android Vulkan surface and presentation queue are available.
- The DXVK 3.1.1 hardware gate fails on the stock driver: maintenance5, maintenance6 and load_store_op_none are not exposed.
- The DXVK 2.4.1 baseline subset passes feature/extension checks.
- Real `vkCreateDevice` and `vkCreateSwapchainKHR` calls both succeed (VkResult 0); the swapchain has four images.

This supersedes the earlier audit statement that the 2.4.1 logical-device/swapchain test was pending. It is still only a Vulkan smoke test: DXVK is neither linked nor executed, and no D3D9 frame has been presented. The next meaningful milestone is an Android ARM64 DXVK Native build plus a minimal D3D9 create/present test. Keep the desktop/Linux backend and its DXVK 3.1.1 pin unchanged; evaluate 2.4.1 as an Android-specific candidate only.


## Android D3D9 renderer bring-up (2026-11-26)

The updated S24 Ultra report confirms successful Vulkan device creation and swapchain creation. The next test is no longer another feature-enumeration probe: it is a standalone D3D9 runtime using a pinned Android-adapted DXVK Native fork.

- Candidate fork: [Star1xr/dxvk-native-android](https://github.com/Star1xr/dxvk-native-android), pinned at `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`.
- Its base is DXVK Native `native-1.9.2b` (upstream commit `c8dc91fabd00cac11d697ccf07426e798393cd40`). The fork has an Android Meson/NDK build path and SDL2 WSI. It also has mobile D3D9 feature fallbacks.
- Its own GitHub Actions run `42513858066` completed successfully. This is cross-build evidence only, not an S24 runtime result.
- New project: `android-d3d9-probe/`. Its workflow builds SDL2, builds the pinned `libdxvk_d3d9.so`, and packages an ARM64 SDLActivity app. The app uses `SDL_WINDOW_VULKAN`, loads the DXVK D3D9 export, creates a HAL device, and attempts a fixed-function draw plus `Present`.

The Android candidate is separate from the Linux backend; no Linux DXVK pin is changed. The probe contains no original game executable, lifted code, ISO, assets or saves. The first corrected APK build has since completed successfully; the exact
build and static artifact inspection are recorded in the status update below.
No successful on-device D3D9 runtime result is claimed.

A successful on-device triangle test will validate DXVK's Android SDL2 WSI and a minimal D3D9 path. It will not yet validate the game's full D3D9 feature/resource use, video, audio, input, lifecycle, memory mapping or simulation. Those remain later integration gates.


## D3D9 probe build and artifact verification (2026-11-26)

The first corrected Android D3D9 probe APK completed successfully in GitHub
Actions run [45742406462](https://github.com/Human9283/The-Movies-Android/actions/runs/45742406462)
at commit `589c13c798fc55b3c7ef533292bd35e55c13d6da`. The artifact
`the-movies-dxvk-d3d9-probe-arm64` was downloaded and independently
inspected.

Observed in the packaged APK:

- `libSDL2.so`, `libmain.so`, `libdxvk_d3d9.so` and
  `libc++_shared.so` are all ELF64 AArch64 shared libraries.
- The three application libraries list the same packaged
  `libc++_shared.so` in `DT_NEEDED`; DXVK also depends on SDL2 and
  Android's Vulkan loader.
- Every `PT_LOAD` segment in all four libraries has `0x4000` (16 KB)
  alignment.
- The uncompressed native-library payloads in the APK start at offsets
  divisible by 16 KB.
- The APK includes the DXVK and SDL2 license notices.

This establishes a successful cross-build, APK packaging, runtime dependency
layout and 16 KB ELF/ZIP alignment. It does not establish that Android's
runtime linker can load the libraries on the target handset, that
`Direct3DCreate9`/device creation succeeds, or that a D3D9 frame is
presented. The user previously reported immediate exit with an earlier APK;
the corrected artifact still requires a fresh physical-device test.

A later CI attempt failed while Gradle fetched Maven Central dependencies
because the repository returned HTTP 429. Native SDL2 and DXVK compilation
had already succeeded in that run. The workflow now uses Gradle dependency
caching and retries this specific transient rate-limit failure. The latest
workflow also adds explicit 16 KB ELF and APK alignment checks. See
[ANDROID-FEASIBILITY.md](ANDROID-FEASIBILITY.md) for the current validation
sequence.


## Current status update: D3D9 backend proven on the target phone (2026-11-26)

The historical audit above predates the successful physical-device D3D9
smoke test. The separate `android-d3d9-probe/` app has now been run on a
Samsung Galaxy S24 Ultra (Adreno 750) and reports successful SDL2
initialization, SDL Vulkan surface creation, DXVK Native 1.9.2 loading,
`Direct3DCreate9`, D3D9 `CreateDevice`, fixed-function triangle rendering
and `Present`. The corresponding ARM64 APK 0.8.0 was built in
[workflow run 45793972336](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).

This proves the standalone Android DXVK D3D9 backend path on this device.
It does not prove the lifted game host, guest-memory mapping, PE loader,
game-specific D3D9 bridge or original game workload on Android. The
diagnostic workflow's compile of `src/host/tm_d3d9_dxvk.cpp` is
object-only and is not an Android link of the game runtime.

### Updated implementation order

1. Preserve the Linux desktop build and its regression tests; no Linux
   DXVK pin or runtime change is required for this Android result.
2. The Android SDL entry point now exists in `src/host/tm_host_android.c`.
   It calls `tm_host_run()`, validates the user's `Movies.exe` and
   `Data/`, and supplies separate app-private game/user/system paths.
3. The Android probe CMake target now compiles the portable host C modules,
   Android SDL entry point, DXVK guest bridge, pcrecomp guest-memory source
   and PE loader as ARM64 objects. This is compile-only validation; none of
   these objects are linked into the diagnostic APK.
4. Add a dedicated Android game Gradle/CMake target and link the full host,
   pcrecomp/win32hle runtime and user's locally generated lifted C.
5. Validate the 4 GiB guest-memory reservation, PE mapping/relocation and
   import binding on the phone; source compilation alone does not prove
   those runtime behaviors.
6. Add a supported game-data import flow, then exercise the actual game's
   D3D9 workload, lifecycle, touch input, audio/video and save/load.

The top-level `CMakeLists.txt` remains Linux-desktop-only today: it
requires desktop pkg-config SDL2/SDL2_ttf and defines the `tm_host`
executable. The diagnostic Android Gradle projects are not a substitute
for that missing game target. Do not mark the Android port complete until
the original game itself launches and renders on-device.


## Full-lift and Android application shell (26 November 2026)

The private CD 1.1 pipeline completed a full lift from the Russian 1.1
Movies.exe. The generated artifact is retained only in the private
`the-movies-gamedata` repository; generated proprietary C is not committed
here.

- 85,910 functions/entries lifted into 215 C translation units.
- 0 lift exceptions and 0 not-lifted stubs.
- The catalog reports 85.64% coverage of the PE .text section (7,580,053
  of 8,851,456 bytes).
- 559 extent walks did not reach a recognized terminator. In pcrecomp's
  `true_extent` metric this is a control-flow cleanliness warning, not a
  code-generation exception; it can include non-returning loops.
- 7 unresolved indirect-tail targets remain. Six are outside the PE .text
  range; one, 0x0086AAC6, is inside .text but absent from the dispatch table.
  These need targeted inspection and must be monitored in runtime traces.

A separate `android-game/` Gradle project and Android-specific CMake target
now exist on the active feature branch. The app shell imports an already
installed game folder through Android's Storage Access Framework and copies
it to app-private storage. The CMake target is designed to link the full
lift, pcrecomp/win32hle runtime, SDL2 and the tested Android DXVK Native
D3D9 backend into `libmain.so`.

**Status boundary:** the CMake/Gradle integration is new and has not yet
passed a full Android link. The D3D9 probe remains the only physical-device
runtime proof. The next gate is the private ARM64 APK build, followed by
guest-memory reservation and PE mapping tests on the S24 Ultra.
