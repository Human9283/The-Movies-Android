# Android Vulkan capability diagnostic

## Purpose

This diagnostic APK measures Vulkan capabilities and Android surface
presentation on an Android ARM64 device. It does not include DXVK, the game
host, lifted game code, game data or copyrighted assets.

## Measurements

The application reports:

- Vulkan loader and physical-device API versions;
- GPU vendor/device identifiers and driver version;
- graphics and presentation queue families;
- selected Vulkan extensions and feature bits;
- Android surface capabilities;
- logical-device and swapchain creation;
- swapchain image clear and presentation.

The report is displayed in the app and can be copied to the clipboard.

## Galaxy S24 Ultra result

Device: Samsung Galaxy S24 Ultra, Snapdragon 8 Gen 3, Adreno 750.

- Vulkan loader API: 1.4.0.
- Physical-device Vulkan API: 1.3.128.
- Driver version: 2150604801.
- Graphics and presentation queues: available.
- `VK_KHR_swapchain`, `VK_EXT_depth_clip_enable` and
  `VK_EXT_robustness2`: exposed.
- `VK_KHR_maintenance5`, `VK_KHR_maintenance6` and
  `VK_KHR_load_store_op_none`: not exposed.
- The strict DXVK Native 3.1.1 extension gate therefore fails on this
  stock driver.
- The tested DXVK 2.4.1 baseline subset created a Vulkan logical device
  and a four-image swapchain; clear and present completed.

The 2.4.1 measurement covers only the feature subset enabled by this
probe. It does not establish compatibility with the complete upstream
DXVK 2.4.1 runtime. The separate Android-adapted DXVK Native 1.9.2 D3D9
diagnostic has passed on this device; see
[android-d3d9-probe](../android-d3d9-probe/README.md).

## Build

Open this directory in Android Studio. The validated project configuration
uses:

- Android SDK Platform 36;
- Android Build-Tools 36.0.0;
- NDK 28.2.13676358;
- CMake 3.22.1;
- JDK 17;
- ABI `arm64-v8a`.

The APK is built by
[the Android Vulkan probe workflow](https://github.com/Human9283/The-Movies-Android/actions/workflows/android-vulkan-probe.yml).
Download the `the-movies-vulkan-probe-arm64-debug` artifact from a
successful run.

## Collect a report

1. Install and open the diagnostic APK.
2. Wait for the capability report.
3. Tap **Скопировать отчёт**.
4. Include the device model, SoC, Android build and complete report when
   reporting a result.

No network connection, login or runtime storage permission is required.

## Logcat

```powershell
adb logcat -c
adb shell monkey -p com.human9283.themoviesprobe 1
adb logcat -d -s TM-VK-PROBE:I TM-VK-PROBE:E
```

A Vulkan diagnostic pass is not evidence that the game runs on Android.
