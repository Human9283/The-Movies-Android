# The Movies — Android Vulkan capability probe

This is a deliberately separate diagnostic APK. It does **not** include the
lifted game, game data, DXVK, or the main runtime. It checks the actual
Android device before the project attempts a DXVK port.

## Why this probe exists

The game currently uses DXVK Native 3.1.1. The pinned upstream source
(`doitsujin/dxvk`, commit
`b1a1c99ab52b687cf950d62c88bc2fa316b41663`) requires Vulkan 1.3,
a graphics queue, `VK_KHR_swapchain`, and a set of mandatory core and
extension features. This probe uses that pinned source's
`DxvkDeviceCapabilities::getFeatureList()` as its reference. It logs
device properties, queue/presentation support, required extension
availability, core Vulkan 1.1/1.2/1.3 features, and selected extension
features. It also creates an Android Vulkan surface and queries its
capabilities.

It deliberately does not create a Vulkan device or run DXVK. A passing
report is a feasibility signal, not proof that DXVK links or renders on
Android.

## Build

Open the `android-probe` directory in Android Studio. Install:
- Android SDK Platform 36;
- Android SDK Build-Tools;
- Android NDK 28.2.13676358;
- CMake 3.22.1;
- JDK 17 (for this Android Gradle Plugin line).

Build the `app` debug variant. The only packaged ABI is `arm64-v8a`.
The project uses Android `NativeActivity` and NDK `android_native_app_glue`;
no SDL or game code is involved in this first hardware check.

## Run and collect the report

Install and launch **The Movies Vulkan Probe** on the phone. Then, from
PowerShell with Android platform-tools installed:

```powershell
adb logcat -c
adb shell monkey -p com.human9283.themoviesprobe 1
adb logcat -d -s TM-VK-PROBE:I TM-VK-PROBE:E
```

Save the complete output and send it back. The important lines include
`GPU`, `Vulkan API`, each `DXVK required extension`, each `feature`,
`queue graphics/present`, and `surface capabilities`.

If Android Studio reports a CMake or NDK error, send the complete first
error block rather than changing versions at random.
