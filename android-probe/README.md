# The Movies — Android Vulkan capability probe

This is a deliberately separate diagnostic APK. It does **not** include the
lifted game, game data, DXVK, or the main runtime. It checks the actual
Android device before the project attempts a DXVK port.

## What it checks

The game currently uses DXVK Native 3.1.1. The pinned upstream source
(`doitsujin/dxvk`, commit
`b1a1c99ab52b687cf950d62c88bc2fa316b41663`) requires Vulkan 1.3,
a graphics queue, `VK_KHR_swapchain`, and a set of mandatory core and
extension features. This probe uses that pinned source's
`DxvkDeviceCapabilities::getFeatureList()` as its reference. It reports
device properties, queue/presentation support, required extension
availability, core Vulkan 1.1/1.2/1.3 features, and selected extension
features. It also creates an Android Vulkan surface and queries its
capabilities.

It deliberately does not create a Vulkan device or run DXVK. A passing
report is a feasibility signal, not proof that DXVK links or renders on Android.

## Use on the phone

1. Install and open **The Movies Vulkan Probe**.
2. Wait for the report to appear on screen.
3. Tap **Скопировать отчёт**.
4. Return to ChatGPT and paste the copied text into the conversation.

The probe needs no internet connection, account, login, storage permission,
or other Android permission. It only queries the local Vulkan driver.

## Build

Open the `android-probe` directory in Android Studio. Install:
- Android SDK Platform 36;
- Android SDK Build-Tools;
- Android NDK 28.2.13676358;
- CMake 3.22.1;
- JDK 17 (for this Android Gradle Plugin line).

Build the `app` debug variant. The only packaged ABI is `arm64-v8a`.
The app uses a small Java Activity and a JNI bridge to pass an Android
`Surface` to the native Vulkan probe. No SDL or game code is involved.

## Optional logcat fallback

If the on-screen report is blank, the same diagnostic lines are written to
Android logcat under tag `TM-VK-PROBE`. They can be collected with ADB:

```powershell
adb logcat -c
adb shell monkey -p com.human9283.themoviesprobe 1
adb logcat -d -s TM-VK-PROBE:I TM-VK-PROBE:E
```
