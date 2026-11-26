# Android ARM64: what is known, what is not

This document separates what was read or measured from what is assumed,
for running the port on Android ARM64 with DXVK Native as the Direct3D 9
backend ([DXVK-INTEGRATION.md](DXVK-INTEGRATION.md)). The game runtime has not run on Android. A separate diagnostic APK now builds in CI, but no physical-device report has been collected yet.

Tags: [src] read from source at a stated revision; [static] read from
`Movies.exe` 1.1; [run] measured here;
[doc.] upstream documentation; [hyp.] not verified.

## 1. Facts

- **No Android support upstream [src].** DXVK v3.1.1 (commit `b1a1c99`)
  has no Android code path. The source has no `__ANDROID__` check, and the
  build files do not mention Android. Its native build targets
  "not Windows" generically (`platform != 'windows'` in
  [meson.build](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/meson.build)).
  The README describes DXVK Native for Linux. **This project does not claim
  that DXVK supports Android.** Using it there is this project's own
  undertaking.
- **Window system [src].** DXVK's SDL2 window system gets its Vulkan
  instance extensions from `SDL_Vulkan_GetInstanceExtensions` and its
  surface from `SDL_Vulkan_CreateSurface`
  ([wsi_platform_sdl2.cpp](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/wsi/sdl2/wsi_platform_sdl2.cpp),
  [wsi_window_sdl2.cpp](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/wsi/sdl2/wsi_window_sdl2.cpp)).
  It contains no X11 or Wayland code of its own, so the surface type is
  SDL's to choose. On Android that is `VK_KHR_android_surface`. [hyp.]
  SDL 2's Android video driver implements both calls
  (`src/video/android/SDL_androidvulkan.c` in SDL 2). This is not yet
  exercised here.
- **Vulkan 1.3 required [src].** An adapter below Vulkan 1.3 is rejected
  ([dxvk_instance.h](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_instance.h#L12),
  [dxvk_device_info.cpp](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_device_info.cpp#L746)).
- **Required features [src].** Every feature below is marked required in
  [`getFeatureList`](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_device_info.cpp#L802).
  An adapter missing any one of them is rejected with "Device does not
  support required feature '…'". No part of DXVK can run on that adapter.

  | Group | Required |
  |---|---|
  | Vulkan 1.0 core | `depthBiasClamp`, `depthClamp`, `dualSrcBlend`, `fillModeNonSolid`, `fragmentStoresAndAtomics`, `fullDrawIndexUint32`, **`geometryShader`**, `imageCubeArray`, `independentBlend`, `multiDrawIndirect`, `multiViewport`, `occlusionQueryPrecise`, `robustBufferAccess`, `sampleRateShading`, `samplerAnisotropy`, `shaderClipDistance`, **`shaderCullDistance`**, `shaderImageGatherExtended`, `shaderInt16`, **`shaderInt64`**, `shaderSampledImageArrayDynamicIndexing`, **`textureCompressionBC`** |
  | Vulkan 1.1 | `shaderDrawParameters`, `storageBuffer16BitAccess` |
  | Vulkan 1.2 | `bufferDeviceAddress`, `descriptorIndexing`, `storageBuffer8BitAccess`, `descriptorBindingSampledImageUpdateAfterBind`, `descriptorBindingUpdateUnusedWhilePending`, `descriptorBindingPartiallyBound`, `hostQueryReset`, `runtimeDescriptorArray`, `samplerMirrorClampToEdge`, `scalarBlockLayout`, `shaderInt8`, `timelineSemaphore`, `uniformBufferStandardLayout`, `vulkanMemoryModel` |
  | Vulkan 1.3 | `inlineUniformBlock`, `computeFullSubgroups`, `dynamicRendering`, `maintenance4`, `shaderDemoteToHelperInvocation`, `shaderZeroInitializeWorkgroupMemory`, `subgroupSizeControl`, `synchronization2` |
  | Extensions | `VK_EXT_depth_clip_enable` (`depthClipEnable`), `VK_EXT_robustness2` (`robustBufferAccess2`, `nullDescriptor`), `VK_KHR_load_store_op_none`, **`VK_KHR_maintenance5`**, **`VK_KHR_maintenance6`** |

  Without `VK_EXT_descriptor_heap`, DXVK also requires
  `maxPushConstantsSize` ≥ 256.
- **Measured [run].** This list was met by Mesa 25.2.8 lavapipe on x86-64,
  where the proof of concept passes (DXVK-INTEGRATION.md 3). That says
  nothing about any phone GPU.
- **What the game draws with [run].** The intro, the menus and the studio
  lot use Direct3D 9's fixed-function pipeline only: no shader is created
  (D3D9-REQUIREMENTS.md 4). DXVK still turns that pipeline into its own
  Vulkan shaders, and its device requirements above apply all the same:
  the game asking for less does not lower them. Its textures include
  `DXT1` and `DXT3`, so `textureCompressionBC` is needed by the game
  itself, not only by DXVK.

## 2. Risks, in order

The features in bold above are the ones most likely to be missing from an
Android driver. Each item below is [hyp.] until a device report exists.

1. **`textureCompressionBC`.** Mobile GPUs natively decode ETC2 and ASTC.
   BC (DXT) support on Android drivers is uncommon. [static] `Movies.exe`
   1.1 has 22 instructions in `.text` that push `D3DFMT_DXT1`
   (`0x31545844`) and 19 that push `D3DFMT_DXT3`, for example at
   `0x009512D6` and `0x00931A06`. Comparisons with `DXT5` appear at
   `0x00A5B25D` and nearby. So the game handles DXT formats; which calls
   receive them is not traced yet. Independently of that, DXVK requires the
   feature at device creation even when no BC texture is used.
2. **`geometryShader`.** It is not exposed by every mobile driver. DXVK
   requires it at device creation, not only for D3D10+ geometry shaders.
3. **`shaderInt64`, `shaderCullDistance`, `dualSrcBlend`,
   `multiViewport`.** Their support varies by driver.
4. **`VK_KHR_maintenance5` and `VK_KHR_maintenance6`.** These are recent
   extensions, likely present only in recent drivers.
5. **Vulkan 1.3.** It is common on recent devices, but not universal.

A device that lacks any of these cannot run DXVK 3.1.1 as built. This
holds however complete the rest of the port is.

## 3. How a device answers

There is no phone capability table yet, and no claim about a named GPU.
A standalone Android Vulkan probe now collects the initial hardware
capability report; it is not DXVK itself and does not create a Vulkan device.
See [android-probe/README.md](../android-probe/README.md) for the APK and
report workflow. The next validation stages are:

1. Run the probe on physical Android devices and record model, OS, GPU,
   driver, extension and feature results as [run] entries.
2. Build the selected DXVK revision for Android ARM64 and test D3D9 device
   creation and presentation on devices whose capability reports justify
   proceeding.
3. Record actual DXVK logs and a presented frame; a probe PASS alone is
   not evidence that DXVK or The Movies runs.

If the drivers to be supported lack required features, the options are,
all [hyp.]:
- a driver that has them, such as Mesa's Turnip for Adreno, measured the
  same way;
- an older pinned DXVK with fewer requirements, re-evaluated under the
  same rules;
- a different Direct3D 9 backend.

None is chosen before a device report exists.

## 4. The build for Android ARM64 (not done yet)

| Part | Plan | State |
|---|---|---|
| Lifted game code and the host (C) | NDK clang, `aarch64-linux-android`, the existing CMake build | not built for ARM64 or Android: M2 ran on x86-64 only |
| win32hle guest memory | 4 GB reservation at a base address, as on x86-64 | [hyp.] fits the user address space of ARM64 Android kernels; not measured |
| SDL2 | built from source with the NDK; `SDLActivity` Java glue | not done |
| DXVK Native | meson cross build with an NDK cross file, `-Dnative_sdl2=enabled`, `d3d9` only. Shaders are compiled at build time with the host's `glslangValidator` | not done; [hyp.] DXVK builds against Bionic without source changes |
| Vulkan loader | the system `libvulkan.so` | |
| Packaging | APK with the native libraries; game data from user storage, never in the APK or the repository | not done |

Each row gets a [run] result when it is done.


## 5. Updated upstream check (2026-11-26)

This section supersedes any older general statements above where the
upstream toolchain has changed. The pinned project build remains DXVK
v3.1.1 at commit `b1a1c99`; do not silently change that pin based on
current wiki requirements.

- **Android NDK [doc.].** Google's current LTS is NDK r30,
  `30.0.16248370`. The official download page publishes a Windows x64
  package, so development can remain on the user's Windows 11 machine or
  WSL2. The NDK package is approximately 728 MB before extraction.
  Source: https://developer.android.com/ndk/downloads
- **SDL Android packaging [doc.].** Current SDL Android instructions
  require Android SDK 36 or later and NDK r28c or later. The supported
  application model is a Gradle Android project with SDL's Java
  `SDLActivity`/JNI glue and a native shared library named `main`.
  SDL's own guide explicitly recommends including SDL as a CMake
  subproject/FetchContent dependency and changing the executable target
  to a shared library for Android.
  Source: https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md
- **DXVK upstream status [doc.].** The upstream driver-support page says
  first-party support is not provided for Android or mobile graphics
  drivers. Its current DXVK 3.x guidance describes a Vulkan 1.4-class
  driver and required extensions/features including maintenance5/6,
  robustness2, transform feedback, descriptor indexing, 8/16/64-bit
  integer types and scalar block layout. This is a warning against
  assuming that a phone which reports Vulkan 1.3 can run a current DXVK
  3.x build. The exact pinned v3.1.1 source still needs to be checked
  independently before altering the project's pin.
  Source: https://github.com/doitsujin/dxvk/wiki/Driver-support
- **Android platform Vulkan policy [doc.].** A device that *launches*
  with Android 16 or later must support Vulkan 1.4 if it has a 64-bit ABI
  and is not low-memory. This is not a guarantee that every older device
  updated to Android 16 exposes Vulkan 1.4; actual feature enumeration
  remains mandatory.
  Source: https://source.android.com/docs/core/graphics/implement-vulkan

### Consequences for this repository

1. Use NDK r30 and SDK 36 as the initial reproducible toolchain baseline.
2. Use SDL's maintained Gradle project template rather than writing an
   Activity/JNI lifecycle from scratch. Keep only the minimal project
   changes in this repository.
3. Convert the existing desktop target to a platform-specific target
   graph: Linux `tm_host` executable versus Android `main` shared
   library. Do not link `tm_host_main.c` into Android.
4. Do not make DXVK the first Android milestone. First produce an ARM64
   SDL app that opens a window and runs a small Vulkan capability probe.
   Then compile and test the guest-memory/PE-loader path on Android.
   Only after both pass should the pinned DXVK Native port be attempted.
5. Treat current DXVK Android usage in Winlator/Box64 as adjacent evidence,
   not proof that a native AArch64 ELF application can link DXVK against
   Android's Bionic and SDL WSI.

No APK has been built and no Android device capability has been measured
as part of this update.


## 6. Standalone Android Vulkan probe added

A separate diagnostic project now exists at `android-probe/`. It uses
Android `NativeActivity` and NDK `android_native_app_glue`, not SDL and
not the lifted game. The purpose is to measure the phone's Vulkan driver
before attempting to build DXVK for Bionic.

The probe:
- creates a real `VK_KHR_android_surface` surface from the activity window;
- reports Vulkan loader/device API versions, GPU and driver IDs;
- checks graphics and surface-present queue support;
- checks `VK_KHR_swapchain`, `VK_KHR_depth_clip_enable`,
  `VK_EXT_robustness2`, `VK_KHR_maintenance5`,
  `VK_KHR_maintenance6`, and `VK_KHR_load_store_op_none`;
- queries the mandatory core Vulkan 1.1/1.2/1.3 feature set and the
  required extension features from the exact pinned DXVK source;
- checks the 256-byte push-constant limit and surface capabilities.

The authoritative feature list was read from
`src/dxvk/dxvk_device_info.cpp` at DXVK commit
`b1a1c99ab52b687cf950d62c88bc2fa316b41663`; the required Vulkan API
version is `VK_API_VERSION_1_3` in that same revision's
`src/dxvk/dxvk_instance.h`. This avoids applying requirements from a
newer DXVK release to the project's pinned version.

Build instructions and PowerShell logcat commands are in
[`android-probe/README.md`](../android-probe/README.md). The probe has
not yet been compiled or run on a phone. Its PASS result, if obtained,
means only that the enumerated hardware capabilities meet this subset of
DXVK's gate; it does not prove that DXVK itself builds or links against
Android's Bionic/WSI.

## 5. External Android recompilation reference (2026-11-26)

A useful, directly related reference is
[codepdbh/nfsu2-recompiled](https://github.com/codepdbh/nfsu2-recompiled).
It is another x86 Windows game statically recompiled to C and brought to
Android ARM64. Its public tree contains a real Android NativeActivity app,
a 4-GiB guest-memory implementation, an Android Vulkan WSI for DXVK Native,
and a reproducible preparation script for a pinned DXVK Native
`1.9.2b` source revision.

The reference is architectural evidence, not a drop-in dependency:

- Its Android DXVK target is a separate CMake library and supplies
  `VK_KHR_android_surface` WSI directly, rather than relying on DXVK's
  desktop SDL2 WSI.
- Its DXVK fork is based on Native `1.9.2b`, not this project's Linux
  baseline `3.1.1`. Its patch recipe makes several GPU features optional,
  adds ARM fallbacks, handles Android surface lifetime, and includes
  DXT fallback work. Those changes must be reviewed against this game's
  D3D9 calls before they are considered.
- Its Android application uses NDK `28.2.13676358`, NativeActivity,
  a native shared library, and a separate launcher/activity layer.
- Its README reports playable ARM64 gameplay on Snapdragon 8 Gen 3-class
  hardware. The README's stated Samsung model identifier appears inconsistent
  with its S24 Ultra label, so this is not treated as a verified test of the
  exact target handset.

### Consequence for The Movies

Keep DXVK Native `3.1.1` as the measured Linux backend. For Android, do
not make its strict feature list the only route. Evaluate the Android-specific
DXVK Native `1.9.2b` approach as a second backend candidate, with the
following gates:

1. Build and run a minimal Android Vulkan 1.1 surface/present application.
2. Build the pinned Android DXVK backend and run a standalone D3D9 device test.
3. Compare its actual D3D9 format, depth, texture, render-target and fixed-
   function requirements with `docs/D3D9-REQUIREMENTS.md`.
4. Only then connect the existing guest D3D9 bridge to that backend.

No source from the reference repository is copied by this note. Its project
licensing must be checked at the file/component level before any code reuse.

## 6. Additional DXVK-on-Android references (2026-11-26)

Two further projects provide evidence that a native Android DXVK build is
possible, including with SDL-based windowing:

- [tarek369/GeneralsZH-Android](https://github.com/tarek369/GeneralsZH-Android)
  reports a native Android ARM64 C++ game using SDL3 and DXVK (D3D8 to
  Vulkan), with the main menu and skirmish gameplay working on a OnePlus Pad 2
  with Snapdragon 8 Gen 3 / Adreno 830. Its Android engineering notes describe
  NDK cross-compilation, the SDL3 WSI, Android soname handling, and DXVK build
  changes. The repository is GPL-3.0; treat it as a reference, not code to
  transplant into this MIT project without a separate licence review.

- [MYSOREZ/GeneralsZH-Android-Port](https://github.com/MYSOREZ/GeneralsZH-Android-Port)
  carries a DXVK Native fork/submodule and explicit Android patches. Of
  particular relevance are its Android shared-library naming changes and its
  adaptive Vulkan 1.1 patch: it handles promoted Vulkan 1.2/1.3 features via
  extension fallbacks, and gates feature bits against what the physical driver
  actually reports. It also contains separate Android/Mali fallback patches.
  This demonstrates why a simple API-version check is insufficient: the
  driver may advertise an API level but fail to support a feature that a
  backend unconditionally enables. The repository reports no SPDX licence
  identifier, so no source reuse is assumed.

Together with the NFSU2 recompilation reference above, these projects support
three separate engineering conclusions:

1. Android ARM64 native game runtimes and Android Vulkan WSI are practical.
2. A desktop DXVK build cannot simply be relinked for Android: Bionic,
   sonames, WSI, feature negotiation, swapchain/surface lifetime and device
   quirks need explicit treatment.
3. The Movies' D3D9 backend should be validated with a standalone Android
   D3D9 test before the guest runtime is connected. D3D8 success in Generals
   is useful precedent, but does not prove D3D9 format/resource coverage.

These repositories are references only. No third-party source has been copied
into this project by this documentation change.

## 7. Adreno 750 driver compatibility warning

The Adreno 750 driver database contains reports for Samsung Galaxy S24 Ultra
devices. Its Qualcomm system-driver sample reports Vulkan 1.3.128 and 137
extensions. The comparison with Turnip 26.3.0-R5 lists
`VK_KHR_maintenance5`, `VK_KHR_maintenance6`, and
`VK_KHR_load_store_op_none` among extensions present in Turnip but absent
from that Qualcomm system-driver sample:
https://fauxvkdb.canonspike.com/compare/5143-43051401/e145252b5018bc47/adea90efaa977350

This matters because the current Linux DXVK Native 3.1.1 compatibility gate
requires those extensions. Therefore, **do not assume DXVK 3.1.1 will run on
the stock Adreno 750 Android driver**, even though the driver advertises
Vulkan 1.3. The database is community-submitted and its driver sample is not
a measurement of this exact handset/firmware combination; the on-device probe
remains the deciding test.

Practical Android backend candidates, in priority order:

1. DXVK Native 2.6 with the Android patches and feature fallbacks used by
   the GeneralsX Android ports; build a standalone D3D9 smoke test.
2. DXVK Native 1.10.3 / 1.9.2b Android adaptation if 2.6 still requires
   unsupported extensions or has an incompatible Android WSI.
3. DXVK Native 3.1.1 only with a driver that actually exposes its required
   extension/feature set (for example a tested Turnip path), not by assuming
   the system driver is sufficient.

The Android Vulkan probe should report both the strict 3.1.1 gate and the raw
driver capability list, so a 3.1.1 failure does not get misread as "Android
port impossible".


## 8. Android device probe: first APK build (2026-11-26)

A standalone diagnostic Android application now lives in `android-probe/`.
It is separate from the game runtime and contains no lifted game code, game
data, DXVK libraries, or copyrighted assets.

- **CI build [run].** GitHub Actions successfully built an Android debug
  APK for `arm64-v8a` using Android SDK 36, NDK 28.2.13676358, and CMake
  3.22.1. Build run:
  https://github.com/Human9283/The-Movies-Android/actions/runs/45734393656
- **Application [build].** The APK uses a Java Activity and JNI bridge to
  pass an Android Surface to the native Vulkan probe. It checks the
  Vulkan loader and physical device, device/API/driver properties,
  graphics and presentation queues, selected extensions and features,
  and Android surface capabilities. The report is displayed on-screen
  and can be copied to the clipboard.
- **Not yet measured.** The successful CI run proves compilation and APK
  packaging only. It does not prove that the APK has been run on a
  physical phone, that a phone passes the DXVK 3.1.1 requirements, or
  that DXVK or the game runs on Android. No phone report has been
  collected at this point.
- **Collection.** Install and open the APK, wait for the report, tap
  “Скопировать отчёт”, and paste the text into the project discussion.
  The probe requires no internet connection or runtime Android
  permissions.

The artifact is named
`the-movies-vulkan-probe-arm64-debug` and is attached to the successful
workflow run above. It is a temporary debug diagnostic APK, not a game
APK; GitHub Actions artifact retention applies.
