# Android DXVK Native D3D9 diagnostic

## Purpose

This application validates SDL2, Android Vulkan WSI and the pinned Android
DXVK Native D3D9 backend on a physical Android device. It does not contain
or execute The Movies, the lifted game code, game data, disc images or saves.

## Tested configuration

- Device: Samsung Galaxy S24 Ultra
- SoC/GPU: Snapdragon 8 Gen 3 / Adreno 750
- ABI: `arm64-v8a`
- SDL2: `release-2.30.9`
- Android DXVK Native: `Star1xr/dxvk-native-android`, commit
  `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`
- DXVK Native base: `native-1.9.2b`

Build and on-device report:
[GitHub Actions run 45793972336](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).

## Test sequence

The launcher first performs a Vulkan preflight using an Android
`SurfaceView`. It obtains an `ANativeWindow`, creates
`VK_KHR_android_surface`, enumerates device features, and attempts logical
device, swapchain, clear and present operations.

The SDL/DXVK stage then:

1. Initializes SDL2 and creates its Android Vulkan window.
2. Obtains and validates SDL's Vulkan surface.
3. Loads `libdxvk_d3d9.so`.
4. Calls `Direct3DCreate9(D3D_SDK_VERSION)`.
5. Creates a D3D9 HAL device.
6. Clears the backbuffer, draws a fixed-function triangle and calls
   `IDirect3DDevice9::Present`.

## Physical-device result

The copied report from the S24 Ultra confirms:

| Operation | Result |
|---|---|
| SDL initialization | PASS |
| SDL Vulkan loader and Android surface | PASS |
| DXVK Native library load | PASS |
| `Direct3DCreate9` | PASS |
| D3D9 `CreateDevice` | PASS; 1440 × 3120 backbuffer |
| Fixed-function draw | PASS |
| `Present` | PASS |

The final report line is
`PASS D3D9 fixed-function triangle + Present`.

This validates the diagnostic workload on the tested device and fork. It
does not validate all D3D9 resources, formats or rendering calls made by
The Movies.

## Android SDL WSI adaptation

The pinned fork's SDL Vulkan extension provider originally created a hidden
dummy window. SDL's Android video backend permits one window per Activity,
so the extension query failed. The project build patches the Android path
to reuse SDL window ID 1 and avoids destroying the application-owned
window. The desktop dummy-window path is unchanged.

## Build inputs and checks

The workflow builds SDL2 and `libdxvk_d3d9.so` for `arm64-v8a`, packages
an SDLActivity APK, and verifies packaged native-library architecture,
shared C++ runtime dependencies and 16 KB ELF/APK alignment.

The workflow also compiles the game's D3D9 bridge, Android SDL entry point,
portable host sources and selected pcrecomp sources as ARM64 objects. These
are compile-only checks: they are not linked into this diagnostic APK.

The Android game application and full-link target are documented in
[android-game/README.md](../android-game/README.md). The diagnostic app is
not a substitute for a full-game APK or game-runtime test.

## Build workflow

[Android D3D9 diagnostic workflow](https://github.com/Human9283/The-Movies-Android/actions/workflows/android-d3d9-probe.yml).

Download the `the-movies-dxvk-d3d9-probe-arm64` artifact from a successful
run and install `app-debug.apk`. The app can run without network access.
