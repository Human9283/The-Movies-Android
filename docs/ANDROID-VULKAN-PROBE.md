# Android Vulkan diagnostic

## Build configuration

| Property | Value |
|---|---|
| Project | `android-probe/` |
| ABI | `arm64-v8a` |
| SDK Platform | 36 |
| NDK | 28.2.13676358 |
| CMake | 3.22.1 |
| Application ID | `com.human9283.themoviesprobe` |
| Workflow | [45734393656](https://github.com/Human9283/The-Movies-Android/actions/runs/45734393656) |
| Artifact | `the-movies-vulkan-probe-arm64-debug` |

The APK is a diagnostic application, not a game build.

## Measured operations

The Java Activity passes an Android `Surface` to native code through JNI.
The native probe reports Vulkan loader/device properties, queue families,
extensions, feature bits and surface capabilities. Later probe revisions
also create a logical device and swapchain, clear a swapchain image and
present it.

The probe does not load DXVK or execute The Movies.

## Galaxy S24 Ultra measurement

Device: Snapdragon 8 Gen 3 / Adreno 750.

- Vulkan loader API: 1.4.0.
- Physical-device API: 1.3.128.
- Driver version: 2150604801.
- Graphics and presentation queues: available.
- `VK_KHR_swapchain`, `VK_EXT_depth_clip_enable` and
  `VK_EXT_robustness2`: exposed.
- The strict DXVK 3.1.1 extension gate fails because
  `VK_KHR_maintenance5`, `VK_KHR_maintenance6` and
  `VK_KHR_load_store_op_none` are absent.
- The probe's DXVK 2.4.1 baseline subset created a logical device and a
  four-image swapchain; clear and present completed.

The 2.4.1 result applies only to the feature subset explicitly requested by
the probe. It is not a test of the complete upstream DXVK 2.4.1 runtime.
The Android-adapted DXVK Native 1.9.2 fork has separately passed a D3D9
device/draw/present test on this device:
[android-d3d9-probe](../android-d3d9-probe/README.md).

## Reproduction

1. Download the debug APK from the
   [Vulkan probe workflow](https://github.com/Human9283/The-Movies-Android/actions/workflows/android-vulkan-probe.yml).
2. Install it on an Android ARM64 device.
3. Open the app and wait for the report.
4. Tap **Скопировать отчёт** to copy the complete report.

The application requires no network connection, login or runtime storage
permission.

## Validation boundary

- [x] ARM64 APK builds in GitHub Actions.
- [x] Report display and clipboard copy.
- [x] Physical-device report collected on the Galaxy S24 Ultra.
- [x] Logical-device and swapchain creation.
- [x] Swapchain clear and present.
- [ ] Full The Movies runtime on Android.

The Vulkan probe is a hardware diagnostic only.
