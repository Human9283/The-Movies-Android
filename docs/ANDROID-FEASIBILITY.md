# Android ARM64

## 1. Scope and current status

This document describes the Android ARM64 application target for The Movies
(2005), its native dependencies, device validation and remaining runtime
requirements.

The Android application shell, game-folder importer and full-game CMake
target are present on branch `ci/android-arm64-asm-dispatch`. The full
lifted game has not yet been verified in an Android APK or launched on a
device.

The standalone Android D3D9 diagnostic application has passed on a Samsung
Galaxy S24 Ultra (Snapdragon 8 Gen 3, Adreno 750). This validates the tested
SDL2, Vulkan WSI and Android DXVK Native D3D9 path, but does not validate
the complete game's runtime.

## 2. Android application

Project: [android-game](../android-game/README.md)

- Gradle application with SDLActivity.
- ABI: `arm64-v8a`.
- Minimum Android API: 24.
- Compile SDK: 36.
- NDK: 28.2.
- Native entry point: `src/host/tm_host_android.c`.
- Native shared library: `libmain.so`.
- Rendering backend: Android DXVK Native D3D9.
- Game data is imported by the Android Storage Access Framework (SAF) into
  app-private storage. Game files are not included in the APK or public
  repository.

The importer accepts an already installed game directory. It validates
`Movies.exe` and `Data/` before committing the import. Copying uses a
staging directory and backup/rollback so an interrupted import does not
replace a valid existing installation with a partial copy.

The Android entry point calls the shared `tm_host_run()` bootstrap with
separate app-private game, user and system paths.

## 3. Native build

The Android CMake target links:

- Android SDL2;
- the Android DXVK Native D3D9 library;
- the shared host and platform shims;
- pcrecomp/win32hle runtime sources;
- the generated C translation units for the supported CD 1.1 executable.

The generated game C and game data are obtained by private CI and are not
stored in this public repository. The private workflow splits oversized
generated C files at function boundaries into chunks of at most 3 MiB,
validates function/symbol preservation, and emits the dispatch table as
AArch64 assembly instead of compiling its large C initializer. The Android
target excludes `recomp_dispatch.c`; generated C chunks use `-O0 -g0` and
`RECOMP_LOCAL_REGS`, while Ninja compilation is serialized to one job.
These settings are intended to lower peak compiler memory during the first
ARM64 link.

The Android CMake wrapper includes the repository's shared top-level
CMake source configuration. Generated C files receive the
`RECOMP_LOCAL_REGS` source property and the low-memory `-O0 -g0` flags; these
source properties are not applied to unrelated host or dependency sources.
The assembly dispatch table has a compile-time ABI guard for 64-bit pointers,
field offsets and the 16-byte entry size.

The application loads native libraries in this order:

1. `SDL2`
2. `SDL2_ttf`
3. `dxvk_d3d9`
4. `main`

Build inputs and the Gradle invocation are documented in
[android-game/README.md](../android-game/README.md).

## 4. Pinned Android dependencies

| Component | Version / revision | Purpose |
|---|---|---|
| Android NDK | 28.2 | ARM64 native compilation |
| Android SDK | compile SDK 36 | APK build |
| SDL2 | 2.30.9 | Android activity, window and input integration |
| Android DXVK Native | `Star1xr/dxvk-native-android`, commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` | D3D9 implementation over Vulkan |
| DXVK Native base | `native-1.9.2b`, upstream base `c8dc91fabd00cac11d697ccf07426e798393cd40` | Base revision of the Android fork |

The Android DXVK dependency is a community-maintained fork, not an
upstream-supported Android release. Preserve its licence and third-party
notices when distributing builds.

The Linux desktop target remains on DXVK Native 3.1.1. The Android fork
does not change the Linux dependency or build configuration.

## 5. Device validation

### Test device

- Samsung Galaxy S24 Ultra
- SoC: Snapdragon 8 Gen 3
- GPU: Adreno 750
- ABI: arm64-v8a

### Standalone D3D9 probe

The diagnostic APK uses SDL2 and the pinned Android DXVK Native fork. It
does not contain the lifted game or proprietary game data.

The on-device report confirms:

| Test | Result |
|---|---|
| SDL initialization | PASS |
| SDL Vulkan loader | PASS |
| SDL Android Vulkan surface | PASS |
| DXVK Native library load | PASS |
| DXVK version | 1.9.2 |
| `Direct3DCreate9` | PASS |
| `IDirect3D9::CreateDevice` | PASS; 1440 × 3120 backbuffer |
| Fixed-function clear/draw | PASS |
| `IDirect3DDevice9::Present` | PASS |

Build and device-test record:
[workflow run 45793972336](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).

This test verifies D3D9 device creation and presentation for the diagnostic
workload on this device. It does not establish compatibility with every
D3D9 resource, format or rendering operation used by The Movies.

### Vulkan driver notes

The stock Adreno 750 driver reports physical-device Vulkan API 1.3.128.
The initial capability probe found that it does not expose
`VK_KHR_maintenance5`, `VK_KHR_maintenance6` or
`VK_KHR_load_store_op_none`, which are required by the project's pinned
Linux DXVK 3.1.1 build.

The Android D3D9 test therefore uses the separately pinned Android DXVK
Native 1.9.2 fork. The successful D3D9 probe is the relevant device
measurement for that fork; it must not be generalized to DXVK 3.1.1.

## 6. APK and native-library checks

The diagnostic APK CI validates:

- all packaged native libraries use the AArch64 ELF format;
- shared C++ runtime dependencies resolve to the packaged
  `libc++_shared.so`;
- ELF `PT_LOAD` segments use 16 KB alignment;
- uncompressed native libraries in the APK meet 16 KB alignment checks.

The JNI library and DXVK use the NDK shared C++ runtime. The Android build
removes the DXVK fork's `-static-libstdc++` flag so the application uses
the packaged NDK runtime consistently.

These checks establish binary architecture, dependency and packaging
properties. They do not establish successful application startup or game
execution.

## 7. Full-game build status

The full-game Android target is built in a private workflow so the generated
C translation units remain private:

[Latest private low-memory/assembly-dispatch experiment](https://github.com/Human9283/the-movies-gamedata/actions/runs/45887244868); [preceding experiment](https://github.com/Human9283/the-movies-gamedata/actions/runs/45885363769).

At the documentation update on 2026-11-26, both runs had been observed in
progress. The newer run had completed dispatch generation, source splitting
and Android dependency/toolchain setup; the full native link, APK checks and
artifact upload were not yet confirmed. No full-game APK is claimed.

The lift input currently records:

- 85,910 lifted entries;
- 215 generated C translation units;
- zero lift exceptions;
- zero not-lifted stubs;
- 559 extent walks without a recognized terminator;
- 7 unresolved indirect-tail targets.

The last two items remain analysis risks. Zero lift exceptions does not
prove semantic correctness or complete runtime coverage.

A successful native link and APK package will prove compilation and
packaging only. The resulting application still requires on-device
validation. Check the linked workflow runs for live status; the status above
is a snapshot from the documentation update, not a live indicator.

## 8. Remaining runtime validation

The following stages are not yet verified for the full game on Android:

1. Install and start the full-game APK.
2. Reserve the guest 4 GiB address space.
3. Map the PE image and apply relocations.
4. Bind required imports under Android/Bionic.
5. Reach the lifted CRT initialization and `WinMain`.
6. Create the game's D3D9 device through the Android DXVK backend.
7. Render the first frame produced by the game.
8. Verify menu navigation and studio-lot rendering.
9. Verify touch input, pause/resume, audio, video and save/load.

The standalone D3D9 probe is not evidence that any of these game-runtime
stages have passed.

## 9. Related implementation documents

- [Android application target](../android-game/README.md)
- [Android D3D9 diagnostic application](../android-d3d9-probe/README.md)
- [Android Vulkan diagnostic application](../android-probe/README.md)
- [D3D9 requirements of the game](D3D9-REQUIREMENTS.md)
- [DXVK integration on Linux](DXVK-INTEGRATION.md)
- [Android host audit](ANDROID-PORT-AUDIT.md)
