# The Movies — Android Vulkan capability probe

This is a separate diagnostic APK. It contains no lifted game code, game assets, DXVK binaries, or proprietary game data.

## Current status

The first Samsung Galaxy S24 Ultra (Snapdragon 8 Gen 3, Adreno 750) report showed Vulkan 1.3.128, graphics and presentation queues, swapchain support, and all reported Vulkan 1.3 core features passing. The driver does not expose the DXVK 3.1.1 extensions `VK_KHR_maintenance5`, `VK_KHR_maintenance6`, and `VK_KHR_load_store_op_none`.

This does **not** rule out DXVK 2.4.x. In upstream DXVK v2.4.1, `VK_EXT_robustness2` and `VK_KHR_swapchain` are required, while `VK_KHR_maintenance5`, `VK_EXT_depth_clip_enable`, and `VK_EXT_transform_feedback` are optional. The updated probe checks the baseline features DXVK 2.4.1 enables unconditionally, attempts `vkCreateDevice`, and then attempts Android swapchain creation. This remains a smoke test, not proof that DXVK Native itself links or renders.

Upstream references:
- [DXVK v2.4.1 adapter feature checks](https://github.com/doitsujin/dxvk/blob/v2.4.1/src/dxvk/dxvk_adapter.cpp)
- [DXVK v2.4.1 extension requirements](https://github.com/doitsujin/dxvk/blob/v2.4.1/src/dxvk/dxvk_extensions.h)
- [DXVK driver support notes](https://github.com/doitsujin/dxvk/wiki/Driver-support)

## Download and use

Open the [Android Vulkan probe workflow](https://github.com/Human9283/The-Movies-Android/actions/workflows/android-vulkan-probe.yml). Open the latest successful run on branch `audit/android-arm64-next-steps`, then download the artifact `the-movies-vulkan-probe-arm64-debug`. Install the APK, open it, wait for the report, and tap **Скопировать отчёт**. Paste the report into the project discussion.

## Checks

- Vulkan loader and physical-device API versions;
- GPU identity, device limits, graphics and present queue families;
- extensions and feature support used by the DXVK 3.1.1 gate;
- DXVK 2.4.1 baseline extension and feature availability;
- actual `vkCreateDevice` with the selected baseline features;
- Android surface capabilities and attempted swapchain creation.

The app does not include DXVK, Wine, the game executable, or game assets. A successful device/swapchain smoke test is useful evidence, but does not substitute for building and running the real runtime.

## Community reports

Please include exact phone model and SoC, Android version/build number, and the complete copied report. This will help build a compatibility matrix across Adreno, Mali, and other Android GPUs. Do not post ISO images or copyrighted game assets.

## Build

Open `android-probe` in Android Studio. Required components: Android SDK Platform 36, Build-Tools 36.0.0, NDK 28.2.13676358, CMake 3.22.1, and JDK 17. Only `arm64-v8a` is packaged.

## Logcat fallback

```powershell
adb logcat -c
adb shell monkey -p com.human9283.themoviesprobe 1
adb logcat -d -s TM-VK-PROBE:I TM-VK-PROBE:E
```