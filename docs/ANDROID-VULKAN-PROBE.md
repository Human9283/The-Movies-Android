# Android Vulkan diagnostic APK

This page tracks the standalone Android hardware probe. It is **not** an
Android build of The Movies and does not include game code, game data, DXVK,
disc images, or copyrighted assets.

## Build

- Project: `android-probe/`
- ABI: `arm64-v8a`
- Toolchain: Android SDK 36, NDK 28.2.13676358, CMake 3.22.1
- App ID: `com.human9283.themoviesprobe`
- CI build: [workflow run 45734393656](https://github.com/Human9283/The-Movies-Android/actions/runs/45734393656)
- Artifact: `the-movies-vulkan-probe-arm64-debug`

The APK is a diagnostic artifact, not a game release.

## What it measures

The app passes an Android `Surface` to native code through JNI and checks:

- Vulkan loader and physical-device API versions;
- GPU name, driver version, vendor and device IDs;
- graphics and presentation queue support;
- extensions and feature bits used by the pinned DXVK Native 3.1.1 compatibility gate;
- Android Vulkan surface capabilities;
- logical-device, swapchain, clear and present operations in later probe revisions.

The report can be copied from the app UI. This probe does not load DXVK or
launch The Movies. Its device and swapchain results are a hardware baseline,
not a substitute for the separate SDL2/DXVK D3D9 runtime test.

## Measured device: Samsung Galaxy S24 Ultra

The report was collected on Snapdragon 8 Gen 3 / Adreno 750:

- Vulkan loader API: 1.4.0.
- Physical-device Vulkan API: 1.3.128.
- Driver version: 2150604801.
- Graphics and presentation queues are available.
- `VK_KHR_swapchain`, `VK_EXT_depth_clip_enable` and
  `VK_EXT_robustness2` are exposed.
- The strict DXVK 3.1.1 gate fails because
  `VK_KHR_maintenance5`, `VK_KHR_maintenance6` and
  `VK_KHR_load_store_op_none` are not exposed.
- The tested DXVK 2.4.1 baseline subset passed logical-device creation;
  the probe created a four-image swapchain, cleared an image and presented it.

The 2.4.1 result is limited to the exact feature subset enabled by the probe.
It does not claim that unmodified upstream DXVK 2.4.1 is Android-compatible.
The separate Android-adapted DXVK Native 1.9.2 fork has since passed its
own D3D9 runtime smoke test on this phone; see
[the D3D9 probe notes](../android-d3d9-probe/README.md).

## Collect a report

1. Download the debug APK from the successful workflow run above.
2. Install it on an Android ARM64 phone and open **The Movies Vulkan Probe**.
3. Wait for the report and tap **Скопировать отчёт**.
4. Review the hardware and driver identifiers before sharing the report.

The app requires no internet connection, login or runtime storage permission.
It queries the local Vulkan implementation.

## Validation boundary

- [x] Android ARM64 debug APK builds in GitHub Actions.
- [x] Report is presented and can be copied.
- [x] Physical-device report collected on Galaxy S24 Ultra.
- [x] Vulkan logical-device, swapchain, clear and present tested on the phone.
- [x] Separate Android DXVK Native 1.9.2 D3D9 device/draw/present smoke test passed.
- [ ] Integrate the D3D9 backend with the actual game host.
- [ ] Launch The Movies and verify its first genuine frame on Android.

A passing diagnostic APK does not establish that the game runs on Android.
