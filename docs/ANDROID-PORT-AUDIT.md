# Android ARM64 port: implementation audit and next steps

Status: source audit of public repository `main` at
`a6a3ba601ef95d5922e6b66b99653d83121255e0`. This is not a device build
or a claim that Android currently runs the game.

## Executive finding

The existing runtime is a 64-bit host process that executes a 32-bit
Windows guest through lifted C and win32hle. That architecture is not
inherently x86-64-only: the CMake top-level check requires a 64-bit host,
and the generated guest code uses 32-bit guest addresses. An AArch64 host
is therefore a plausible target.

However, the current repository is still a Linux desktop executable, not
an Android application. The Android port is blocked first by application
lifecycle/window integration and build-system assumptions, then by whether
the selected DXVK build and the actual phone Vulkan driver can run.

Do not start by changing the lifted game code or by assuming that DXVK
3.1.1 will work on every Android GPU.

## Findings from source

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

- `src/host/tm_host.c` exposes a conventional `main(argc, argv)`.
- The game directory is passed as a command-line argument and defaults
  to relative host paths such as `tm_user` and `tm_system`.
- There is no Android Gradle project, manifest, Java/Kotlin activity,
  JNI bridge, Android asset/data installer, or APK packaging.
- The runtime currently expects the game directory to be available as a
  normal filesystem path. Android must select a private app data directory
  or a user-imported directory and pass that path to the native runtime.
- The current Linux window is owned by win32hle/SDL. The Android lifecycle
  must create and destroy the SDL window/surface in response to activity
  and native-window lifecycle events; it cannot be treated as a persistent
  desktop window.

### 3. Build-system blockers

The top-level CMake currently assumes a Linux desktop build:

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

The current Android feasibility document correctly records that DXVK
3.1.1 requires Vulkan 1.3 and a substantial feature/extension set,
including geometry shaders, shaderInt64, BC texture compression and
recent maintenance extensions.

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

The next code change should be a narrowly scoped CMake refactor that
separates the desktop executable from reusable runtime sources while
preserving the current Linux build. It should not yet add an APK or claim
Android support. First inspect and test the refactor on the existing Linux
build, then add the NDK target in a separate change.
