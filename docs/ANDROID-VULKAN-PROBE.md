# Android Vulkan diagnostic APK

This page tracks the standalone Android hardware probe. It is **not** an
Android build of The Movies and does not include game code, game data, DXVK,
disc images, or copyrighted assets.

## Current build

- Branch containing the APK project: `audit/android-arm64-next-steps`
- Successful CI run:
  https://github.com/Human9283/The-Movies-Android/actions/runs/45734393656
- Artifact: `the-movies-vulkan-probe-arm64-debug`
- ABI: `arm64-v8a`
- Build toolchain: Android SDK 36, NDK 28.2.13676358, CMake 3.22.1
- App ID: `com.human9283.themoviesprobe`

The workflow run proves that the diagnostic APK compiles and packages. It
does not prove that the app has been run on a physical Android device or
that the device can run DXVK.

## What it measures

The app passes an Android `Surface` to native code through JNI and checks:

- Vulkan loader API version;
- physical GPU name, API version, driver version, vendor and device IDs;
- graphics and presentation queue support;
- selected extensions required by the pinned DXVK Native 3.1.1 build;
- the core and extension feature bits used by that DXVK compatibility gate;
- Android Vulkan surface capabilities.

The check does not create a Vulkan logical device, load DXVK, render a frame,
or launch The Movies. A passing hardware gate is only an initial
compatibility signal.

## Collect a device report

The report records the phone's Vulkan implementation, GPU, driver, queue support,
required extensions and surface capabilities. It helps determine whether the
device meets the graphics requirements being evaluated for the Android port.
It does not test DXVK or run the game.

1. Download the debug APK from the successful workflow run linked above.
2. Install it on an ARM64 Android device and open **The Movies Vulkan Probe**.
3. Wait for the diagnostic report to appear.
4. Use the app's copy-report control.
5. Paste the copied text into a project discussion or GitHub issue.

The app requires no internet connection, login, storage permission, or other
runtime Android permission. It queries the local Vulkan implementation.
The report includes hardware and driver identification; users should review
it before posting publicly.

## Current validation boundary

- [x] Android ARM64 debug APK builds in GitHub Actions.
- [x] Report is presented in the app UI.
- [x] Report can be copied through Android's clipboard.
- [ ] Run on a physical Android device and collect a report.
- [ ] Verify the actual device against the DXVK 3.1.1 feature gate.
- [ ] Build and run DXVK Native on Android.
- [ ] Build and run The Movies on Android.

The APK is a temporary GitHub Actions artifact, not a release. Artifact
retention and public visibility follow the repository's GitHub settings.
