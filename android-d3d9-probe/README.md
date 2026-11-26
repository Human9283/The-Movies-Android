# Android DXVK Native D3D9 smoke test

This is a diagnostic application, not an Android build of The Movies. It
contains no game executable, lifted game code, ISO, assets, saves or user
files.

The Android NDK build additionally compiles the repository's real portable
host C translation units (runtime bootstrap, loader, Win32 shims, VFS,
DirectInput, media and D3D9 host modules), plus pcrecomp's guest-memory and PE
loader sources, as ARM64 object files. The existing `tm_d3d9_dxvk.cpp`
bridge and the new `src/host/tm_host_android.c` SDL entry point are also
compiled against the Android headers. The entry point calls
`tm_host_run()` with explicit app-private paths:
`<SDL files>/game`, `<SDL files>/user` and `<SDL files>/system`; it expects
the user's own `Movies.exe` and `Data/` under the game directory. These are
compile-only checks: none of these objects are linked into the probe APK, and
the user's generated lifted game C is not present in the public repository.
A successful APK build validates source/ABI compatibility, not a runnable
game host.

## Current result: passed on Galaxy S24 Ultra

The user ran the probe on a Samsung Galaxy S24 Ultra (Snapdragon 8 Gen 3 /
Adreno 750). The copied native report ended with:

```text
Direct3DCreate9: PASS
IDirect3D9::CreateDevice: PASS; backbuffer=1440x3120
D3D9 fixed-function clear/draw/present started
PASS D3D9 fixed-function triangle + Present
```

This confirms that the tested Android-adapted DXVK Native 1.9.2 build can
initialize D3D9, create a device on the SDL Vulkan window, execute a
fixed-function draw and present a frame on this phone. It is the first
successful end-to-end D3D9 runtime result in this project on Android.

The successful 0.8.0 ARM64 APK build is recorded in
[GitHub Actions run 45793972336](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).
Artifact: `the-movies-dxvk-d3d9-probe-arm64`.

**Boundary:** this is still a smoke-test application. It does not package or
run the lifted game, the game host, or proprietary game data. It does not
validate the complete set of D3D9 calls/resources used by The Movies,
audio/video, touch input, Android pause/resume, guest-memory reservation,
or save/load.

## What it tests

The launcher first opens a standalone Vulkan preflight screen. It waits for
a Java `SurfaceView.surfaceCreated` callback, obtains the corresponding
`ANativeWindow`, creates `VK_KHR_android_surface`, enumerates device and
extension/feature support, then attempts a minimal logical device, swapchain,
clear and present. The report can be copied even if the later SDL/DXVK stage
fails.

After the preflight has finished, press **Запустить DXVK**. The SDL test:

1. Initializes SDL2's Android Activity.
2. Creates the app's SDL Vulkan window and obtains its `ANativeWindow`.
3. Creates and validates an SDL Vulkan surface.
4. Loads the pinned Android DXVK Native D3D9 library.
5. Calls `Direct3DCreate9(D3D_SDK_VERSION)`.
6. Creates a D3D9 HAL device on the SDL Vulkan window.
7. Clears the backbuffer, draws a fixed-function `XYZRHW | DIFFUSE` triangle,
   and calls `IDirect3DDevice9::Present`.

The expected successful result is a blue background with a light/green
triangle and the status line
`PASS D3D9 fixed-function triangle + Present`. Both reports have copy
buttons; paste the full native trace when reporting a failure.

## Android-specific WSI fix

The pinned fork's SDL2 extension provider originally created a hidden
`Dummy Window` to query Vulkan instance extensions. SDL's Android video
backend supports only one window, so this failed with
`Android only supports one window` after SDL and Vulkan surface preflight
had already succeeded.

The project workflow now patches the Android build to reuse SDL window ID 1
for extension enumeration and skips destroying that application-owned window.
The desktop dummy-window path remains unchanged. This fix was validated by
the successful `Direct3DCreate9`, `CreateDevice` and `Present` report above.

## Build and pinned sources

GitHub Actions builds the ARM64 APK from pinned public sources:

- SDL2 `release-2.30.9`
- Android DXVK Native fork `Star1xr/dxvk-native-android`, commit
  `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`
- pcrecomp headers `7c07a0b42059b6dc4313d9b4ebded6f7a72db8f3` for an
  object-only compile check of the repository's real
  `src/host/tm_d3d9_dxvk.cpp`

The workflow cross-compiles SDL2 and `libdxvk_d3d9.so` for Android ARM64,
builds the APK, checks the packaged native libraries and their shared C++
runtime dependencies, and verifies 16 KB ELF/APK alignment. The bridge
compile check does not package or run the game host.

Open the [Android D3D9 workflow](https://github.com/Human9283/The-Movies-Android/actions/workflows/android-d3d9-probe.yml),
select a successful run, and download artifact
`the-movies-dxvk-d3d9-probe-arm64`. Unzip it and install `app-debug.apk`.

## Next integration stages

1. Add a dedicated Android Gradle/CMake app target and link the new
   `tm_host_android.c` entry point with the complete runtime; the current
   probe APK still does not link it.
2. Build the real lifted runtime and pcrecomp/win32hle dependencies for
   `arm64-v8a`, then validate the 4 GiB guest-address reservation and PE
   image mapping on Android.
3. Connect the tested DXVK backend to the game's guest D3D9 bridge in the
   linked Android host.
4. Add a supported game-data import flow into the app-private `game/`
   directory (the entry point currently only validates that directory).
5. Handle Android surface lifecycle, pause/resume, touch-to-mouse mapping,
   audio/video and save/load.
6. Run the original game and capture its first genuine frame.

The Android fork is a community adaptation, not an upstream-supported DXVK
Android release. The Linux project's DXVK Native 3.1.1 pin and Linux runtime
remain unchanged.
