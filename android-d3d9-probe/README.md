# Android DXVK Native D3D9 smoke test

This is a diagnostic application, not an Android build of The Movies. It contains no game executable, lifted game code, ISO, assets, saves or user files.

**Validation status:** a corrected APK build completed successfully in [workflow run 45742406462](https://github.com/Human9283/The-Movies-Android/actions/runs/45742406462). Its packaged native libraries were independently inspected: all are AArch64, use the packaged shared C++ runtime, and have 16 KB ELF load-segment alignment. Earlier APKs did not start successfully on the target phone. The corrected APK has **not yet been tested on the phone**. Do not treat a green CI build as a successful device test; acceptance requires a visible rendered triangle and a copied report showing D3D9 `CreateDevice` and `Present` succeeded.

## Startup and diagnostic sequence

The launcher first opens a standalone Vulkan preflight screen. It does not
load SDL or DXVK. It waits for a Java `SurfaceView.surfaceCreated` callback,
obtains the corresponding `ANativeWindow`, creates
`VK_KHR_android_surface`, enumerates the device and extension/feature support,
then attempts a minimal logical device, swapchain, clear and present. The
report can be copied even when the later SDL/DXVK stage crashes.

After the preflight has finished, press **Запустить DXVK**. The SDL test then
checks its own `ANativeWindow`, creates and destroys a Vulkan surface through
`SDL_Vulkan_CreateSurface` (retrying briefly), and only then loads DXVK. This
distinguishes a Java/Android Surface failure from an SDL WSI or DXVK failure.
The preflight's DXVK 3.1.1 / 2.4.1 rows are reference comparisons, not a claim
that the Android fork pinned below has identical feature requirements.

## What it tests

The APK starts SDL2's Android Activity and creates an SDL Vulkan window. It then loads the pinned Android DXVK Native D3D9 library and attempts:

1. `Direct3DCreate9(D3D_SDK_VERSION)`
2. D3D9 HAL device creation on the SDL Vulkan window
3. Backbuffer clear
4. A fixed-function `XYZRHW | DIFFUSE` triangle
5. `IDirect3DDevice9::Present`

The expected successful result is a blue background with a light/green triangle and the status line `PASS D3D9 fixed-function triangle + Present`. The Vulkan preflight screen has a **Скопировать отчёт** button; paste its contents into the ChatGPT conversation. The SDL/DXVK screen retains its own **Copy report** button if it starts successfully.

If the app stops early or shows an error, copy the report anyway. It includes the stage and HRESULT or SDL error.

## Build

GitHub Actions builds the ARM64 APK from pinned public sources:

- SDL2 `release-2.30.9`
- Android DXVK Native fork `Star1xr/dxvk-native-android`, commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`
- pcrecomp headers `7c07a0b42059b6dc4313d9b4ebded6f7a72db8f3` for a compile-only check of this repository's real `src/host/tm_d3d9_dxvk.cpp`

The workflow cross-compiles SDL2 and `libdxvk_d3d9.so` with Android NDK r28c, builds the APK, verifies its ARM64 native libraries, and compiles the existing game D3D9 bridge against the Android fork's headers. The bridge compile is object-only: it does not package or run the game host.

Latest build: [Android DXVK D3D9 smoke test](https://github.com/Human9283/The-Movies-Android/actions/workflows/android-d3d9-probe.yml)

Open the successful run, download artifact `the-movies-dxvk-d3d9-probe-arm64`, unzip it, and install `app-debug.apk`.

## Interpretation

A successful screen and report demonstrate that this DXVK Native Android build can create a D3D9 device, execute a fixed-function draw and present through SDL2 on the tested phone. This is a much stronger result than Vulkan feature enumeration, but it does not yet prove that The Movies itself runs. The game's full D3D9 call/resource set, audio/video, input, lifecycle, guest memory and original game data still need integration and validation.

The Android fork is a community adaptation, not an upstream-supported DXVK Android release. The Linux project's DXVK 3.1.1 pin remains unchanged.
