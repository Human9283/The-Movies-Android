# Android ARM64: what is known, what is not

This document separates what was read or measured from what is assumed,
for running the port on Android ARM64 with DXVK Native as the Direct3D 9
backend ([DXVK-INTEGRATION.md](DXVK-INTEGRATION.md)). The game runtime has not run on Android. The Vulkan capability/WSI probe and a separate SDL2 + Android DXVK Native D3D9 smoke test have both been run on a Samsung Galaxy S24 Ultra (Adreno 750); the latter successfully creates a D3D9 device, draws a fixed-function triangle and presents a frame. The game host is not yet integrated into the Android app.

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

A first physical-device capability report is recorded in section 7.
The standalone Vulkan probe is separate from DXVK; its successful device
and swapchain creation do not establish that DXVK itself works. See
[android-probe/README.md](../android-probe/README.md). The next validation
stages are:

1. Preserve the Galaxy S24 Ultra report as the baseline hardware result.
2. Complete the Android ARM64 DXVK Native D3D9 build and inspect the APK's
   packaged ELF dependencies.
3. Run that D3D9 probe on the S24 Ultra and require a visible frame plus
   successful `CreateDevice` and `Present` evidence.
4. Only then connect the tested backend to the game's D3D9 host bridge.

If a target driver lacks features required by a candidate backend, possible
fallbacks are, all [hyp.]:
- a driver that has them, such as Mesa's Turnip for Adreno, measured the
  same way;
- an older pinned DXVK with fewer requirements, re-evaluated under the
  same rules;
- a different Direct3D 9 backend.

The Android-specific DXVK Native fork documented in section 8 is the current
D3D9 test candidate; no fallback is considered validated until it passes on
the target phone.

## 4. Initial Android build plan (historical snapshot; superseded below)

| Part | Plan | State |
|---|---|---|
| Lifted game code and the host (C) | NDK clang, `aarch64-linux-android`, the existing CMake build | not built for ARM64 or Android: M2 ran on x86-64 only |
| win32hle guest memory | 4 GB reservation at a base address, as on x86-64 | [hyp.] fits the user address space of ARM64 Android kernels; not measured |
| SDL2 | built from source with the NDK; `SDLActivity` Java glue | not done |
| DXVK Native | meson cross build with an NDK cross file, `-Dnative_sdl2=enabled`, `d3d9` only. Shaders are compiled at build time with the host's `glslangValidator` | not done; [hyp.] DXVK builds against Bionic without source changes |
| Vulkan loader | the system `libvulkan.so` | |
| Packaging | APK with the native libraries; game data from user storage, never in the APK or the repository | not done |

Each row gets a [run] result when it is done.


## 5. Upstream/toolchain research (2026-11-26; planning snapshot)

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

This paragraph records the state at the time this research section was
written. It is superseded by the Vulkan probe build and S24 Ultra device
measurements in sections 6–8, and by the D3D9 probe bring-up below.


## 6. Standalone Android Vulkan probe

A separate diagnostic project exists at `android-probe/`. It uses a small
Java Activity and JNI bridge, not SDL, NativeActivity, or the lifted game.
The APK builds in GitHub Actions and presents a copyable on-screen report.

The probe:
- creates a real `VK_KHR_android_surface` surface from an Android Surface;
- reports Vulkan loader/device API versions, GPU and driver IDs;
- checks graphics and surface-present queue support;
- reports the DXVK 3.1.1 extension and feature gate from the pinned source;
- checks a baseline subset for DXVK 2.4.1;
- attempts actual Vulkan logical-device creation with that baseline subset;
- attempts Android swapchain creation and reports the result.

The DXVK 3.1.1 feature list is read from `src/dxvk/dxvk_device_info.cpp`
at commit `b1a1c99ab52b687cf950d62c88bc2fa316b41663`. The DXVK 2.4.1
comparison is based on the upstream
[`dxvk_adapter.cpp`](https://github.com/doitsujin/dxvk/blob/v2.4.1/src/dxvk/dxvk_adapter.cpp)
and
[`dxvk_extensions.h`](https://github.com/doitsujin/dxvk/blob/v2.4.1/src/dxvk/dxvk_extensions.h).
The 2.4.1 source marks robustness2 and swapchain as required while
maintenance5, depth clip and transform feedback are optional.

A Vulkan device/swapchain smoke-test PASS is not proof that DXVK Native
links against Bionic, that the D3D9 backend initializes, or that a frame
renders. Build and usage instructions are in
[`android-probe/README.md`](../android-probe/README.md).

## 7. First physical-device report: Galaxy S24 Ultra

The first report was copied from a Samsung Galaxy S24 Ultra with
Snapdragon 8 Gen 3 / Adreno 750.

- Vulkan loader API: 1.4.0; physical-device API: 1.3.128.
- Graphics and presentation queue families are available.
- `VK_KHR_swapchain`, `VK_EXT_depth_clip_enable`, and
  `VK_EXT_robustness2` are exposed.
- The device does not expose `VK_KHR_maintenance5`,
  `VK_KHR_maintenance6`, or `VK_KHR_load_store_op_none`; therefore
  it fails the pinned DXVK 3.1.1 hardware gate.
- Every core and extension feature printed as PASS by the initial probe
  passed; the report was capability enumeration only and did not create a
  Vulkan logical device.

This result rules out the unmodified DXVK 3.1.1 path on this driver, not
Android as a whole. The next candidate is DXVK 2.4.1, whose upstream source
has a less restrictive required-extension set. The updated probe now
attempts a logical device and swapchain with a baseline subset of its
unconditionally enabled features. That result is pending a new APK run
on the phone.

The current project pin remains DXVK 3.1.1 until a real Android build and
runtime test justify changing it.

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


## Physical-device Vulkan smoke test: Galaxy S24 Ultra (2026-11-26, probe 0.3)

The user ran the updated APK on a Samsung Galaxy S24 Ultra (Snapdragon 8 Gen 3 / Adreno 750). This is a measured [run] result, not an inference from a GPU database:

- Vulkan loader API 1.4.0; physical-device Vulkan API 1.3.128; driverVersion 2150604801.
- Graphics and presentation queues are available. The Android surface reports minImageCount 4 and maxImageCount 64.
- DXVK 3.1.1 gate: FAIL because `VK_KHR_maintenance5`, `VK_KHR_maintenance6`, and `VK_KHR_load_store_op_none` are absent.
- DXVK 2.4.1 baseline extensions: PASS; baseline features: PASS.
- `vkCreateDevice`: PASS (VkResult 0).
- `vkCreateSwapchainKHR`: PASS (VkResult 0), four images.

This is materially stronger than capability enumeration: the stock Android driver successfully creates a Vulkan logical device and an Android-presentable swapchain using the probe's DXVK 2.4.1 baseline feature subset. It does **not** prove that the complete DXVK 2.4.1 device feature chain is identical to the probe subset, that DXVK links against Bionic, that DXVK's D3D9 backend initializes, or that a frame from The Movies renders. The probe explicitly does not link or run DXVK.

**Decision:** keep the Linux DXVK 3.1.1 pin unchanged. For Android, promote DXVK 2.4.1 to the next test candidate rather than declaring it compatible. Next gate: build the actual Android ARM64 DXVK Native library (including Android WSI/lifecycle integration), then run a minimal D3D9 device-creation and present test before connecting the game's guest D3D9 bridge. Do not relax or patch DXVK feature requirements solely to make the probe pass.


## Android D3D9 backend candidate (2026-11-26)

The S24 Ultra's probe 0.3 report establishes that the stock Adreno 750 driver exposes the Vulkan 1.3 features and the VK_KHR_swapchain / VK_EXT_robustness2 extensions used by the tested DXVK 2.4.1 baseline. That is not a DXVK runtime test. A separate, Android-adapted DXVK Native candidate has now been selected for the next executable test:

- Repository: [Star1xr/dxvk-native-android](https://github.com/Star1xr/dxvk-native-android)
- Pinned commit: `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`
- Base: DXVK Native `native-1.9.2b`, upstream base commit `c8dc91fabd00cac11d697ccf07426e798393cd40`
- License: Zlib for DXVK; preserve the included license and third-party notices.
- Android adaptation: Meson Android target, NDK toolchain, SDL2 Vulkan WSI, ARM-specific guards, Android logcat logging and a D3D9 mobile-feature profile. Its own Android workflow run `42513858066` completed successfully for its ARM64/ARMv7 build matrix.

This is a community fork, not an upstream-supported Android release. The successful external CI build proves only that the fork can be cross-compiled; it does not prove runtime compatibility with the S24 Ultra. It is chosen over an unmodified DXVK 2.4.1 build because it already contains the Android/Bionic and SDL2 WSI adaptations needed by this project's SDL2 host. The project's Linux DXVK 3.1.1 pin remains unchanged.

A separate `android-d3d9-probe/` app and GitHub Actions workflow have been added on this feature branch. The workflow pins the fork above, builds SDL2 2.30.9 and `libdxvk_d3d9.so` for `arm64-v8a`, then packages a minimal SDLActivity APK. The native test will call `Direct3DCreate9`, create a D3D9 HAL device, clear the backbuffer, draw a fixed-function triangle, and call `Present`. This deliberately tests the same SDL2/Vulkan WSI shape as the game runtime without including the game or its data.

**Validation state:** the workflow has been started, but its build and APK packaging are not yet verified. Even a successful CI artifact will need a physical S24 Ultra run. The acceptance result is a visible blue frame with a green/white triangle and successful D3D9 create/present logs. Only after that should the Android host be connected to the lifted game runtime.


## Probe 0.4: actual Vulkan queue presentation (2026-11-26)

The follow-up diagnostic APK now uses a visible Android SurfaceView and extends the Vulkan smoke test beyond swapchain creation. When the surface supports `VK_IMAGE_USAGE_TRANSFER_DST_BIT`, it acquires a swapchain image, records a transfer clear, transitions the image to `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`, submits it, and calls `vkQueuePresentKHR`. The APK build completed successfully in GitHub Actions run [45736487381](https://github.com/Human9283/The-Movies-Android/actions/runs/45736487381). The new APK has not yet been run on the phone, so actual present remains unmeasured. This is still a Vulkan WSI test and does not load DXVK.


## 9. Current Android D3D9 bring-up status (2026-11-26)

The project has progressed beyond the earlier Vulkan-only diagnostic stage.
The separate `android-d3d9-probe/` application now builds SDL2 2.30.9 and
the pinned Android DXVK Native fork for `arm64-v8a`, packages them in an
SDLActivity APK, and compiles the repository's D3D9 bridge as an object-only
compatibility check. This is still a diagnostic app: it does not include
the lifted game, the game host, or proprietary game data.

### Runtime consistency

Android's NDK guidance recommends using one shared C++ runtime when an
application contains multiple shared libraries that use the C++ standard
library. The probe therefore configures the JNI `main` library and DXVK to use the same NDK
`libc++_shared.so`, packages that runtime once, and checks their packaged
ELF `DT_NEEDED` entries. The CI also checks that every packaged native
library is AArch64, every `PT_LOAD` segment has at least 16 KB alignment,
and the APK's uncompressed native libraries pass `zipalign -P 16`. The DXVK fork's build
script originally added `-static-libstdc++`; the workflow removes that
flag for this application build. This is a project-specific adaptation,
not a change to the fork's upstream repository or to the Linux DXVK build.

### Known validation history

- Earlier APK builds compiled successfully but the user reported that the
  app closed immediately on the Galaxy S24 Ultra. No successful D3D9
  initialization or rendered frame has been observed yet.
- The first shared-runtime CI attempt failed in the verification step
  because it looked for Gradle's `libmain.so` in the SDL/DXVK source
  directory instead of extracting the packaged native libraries from the
  APK. This was a CI verification-path defect, not evidence of a linker
  or device-runtime failure.
- The verification step was changed to extract the APK's
  `lib/arm64-v8a/*.so` files and inspect those exact packaged binaries.
- A separate source review found incorrectly escaped newline and NUL
  character literals in the native probe report buffer. These were fixed:
  the original multi-character character constants were implementation-
  defined when narrowed to `char`, so the report's line endings and NUL
  termination were not being written as intended. This was a real report-
  buffer defect, but it has not been established as the cause of the earlier
  process exit; it is independent of the shared-runtime change.
- **Successful APK build [run].** Workflow run
  [45743035567](https://github.com/Human9283/The-Movies-Android/actions/runs/45743035567)
  at commit `a246e27` completed SDL2 and DXVK builds, Gradle APK
  packaging, shared-runtime/ELF verification, explicit 16 KB ELF and APK
  alignment checks, and artifact upload. The Gradle cache/retry change also
  avoided the earlier Maven Central 429 failure. Artifact:
  `the-movies-dxvk-d3d9-probe-arm64` (artifact ID `15082003202`).
- **Independent artifact inspection [run].** The APK was downloaded and
  inspected outside CI. All four packaged native libraries
  (`libSDL2.so`, `libmain.so`, `libdxvk_d3d9.so`,
  `libc++_shared.so`) are AArch64 ELF files. All have
  `libc++_shared.so` in `DT_NEEDED`; every `PT_LOAD` segment is aligned
  to `0x4000` (16 KB), and each uncompressed native library starts at a
  16 KB-aligned offset in the APK.
- A later build attempt failed during Gradle dependency resolution because
  Maven Central returned HTTP 429. It did not fail in DXVK or native
  compilation. The workflow now caches Gradle dependencies, retries that
  transient error, and adds explicit 16 KB ELF/APK alignment checks.
- **Phone runtime remains unverified.** The successful CI run proves
  compilation, packaging and static ELF checks only. It does not prove
  that the app starts on the Galaxy S24 Ultra or that D3D9 creates a
  device and presents a frame.

### Acceptance gate

Do not treat the Android D3D9 backend as validated until the S24 Ultra
shows the expected rendered frame and the copied report confirms all of:
SDL initialization, Vulkan loading, SDL Vulkan window creation, DXVK
library loading, `Direct3DCreate9`, D3D9 `CreateDevice`, fixed-function
draw and `Present`. If the app still exits before the report appears,
capture the Android crash log and identify the exact failing library or
symbol before changing the backend or lowering DXVK requirements.

The Linux DXVK 3.1.1 build and the Linux game runtime remain unchanged.


## 10. Integrated Android Vulkan / SDL Surface preflight (2026-11-26)

The D3D9 diagnostic APK now starts in a separate Java `VulkanPreflightActivity`
rather than immediately entering SDLActivity. The preflight reuses the
standalone Vulkan probe implementation in a separate `libvulkanpreflight.so`
library. It waits for `SurfaceHolder.Callback.surfaceCreated`, obtains an
`ANativeWindow`, creates `VK_KHR_android_surface`, reports the physical
device's extension list and the `VK_EXT_robustness2` /
`VK_KHR_maintenance5` feature bits, then attempts its Vulkan device,
swapchain, clear and present smoke test. DXVK is not loaded in this stage.

After the report appears, the user can press **Запустить DXVK** to enter the
SDLActivity. The SDL-side native test now:
- retrieves SDL's Android `ANativeWindow` via `SDL_GetWindowWMInfo`;
- checks its dimensions;
- obtains SDL's Vulkan instance-extension list;
- creates a Vulkan instance and calls `SDL_Vulkan_CreateSurface` before
  calling `dlopen("libdxvk_d3d9.so")`;
- retries the SDL surface creation for up to two seconds and reports each
  failed attempt;
- only proceeds to DXVK after the SDL Android surface preflight succeeds.

This deliberately separates three stages: Java Surface / Android WSI,
SDL's Android WSI, and DXVK's D3D9 device initialization. A successful
standalone preflight does not establish that SDL's own surface is ready or
that DXVK can create a device. A failed SDL surface check will now be
reported before DXVK is loaded. No claim is made that the Surface lifecycle
bug was the confirmed cause of the previous crash; the next physical-device
run will determine that.

The staged launcher and SDL WSI instrumentation built successfully in GitHub Actions run [45786134694](https://github.com/Human9283/The-Movies-Android/actions/runs/45786134694), commit `bd05a64`. The APK artifact is `the-movies-dxvk-d3d9-probe-arm64` (ID `15099869888`); CI verified its AArch64 native libraries, separate preflight library, shared C++ runtime dependencies, 16 KB ELF load-segment alignment, and 16 KB APK alignment. The 0.3.0 APK has not yet been run on the Galaxy S24 Ultra; the next measurement is the copied preflight report followed by the SDL/DXVK test.


## 11. Physical Android DXVK D3D9 smoke test: PASS (2026-11-26)

The user ran the Android D3D9 probe on a Samsung Galaxy S24 Ultra
(Snapdragon 8 Gen 3 / Adreno 750). The copied native report confirms:

- SDL initialization: PASS.
- SDL Vulkan loader and Android surface: PASS.
- Android-adapted DXVK Native library load: PASS.
- DXVK reports version 1.9.2.
- `Direct3DCreate9`: PASS.
- `IDirect3D9::CreateDevice`: PASS; backbuffer 1440×3120.
- Fixed-function clear/draw: started.
- `IDirect3DDevice9::Present`: PASS; the probe reports
  `PASS D3D9 fixed-function triangle + Present`.

The corresponding ARM64 APK 0.8.0 was built successfully by
[workflow run 45793972336](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336)
at commit `59e2d3a265c97ff97907dd0508857b07a13f8cdb`. The workflow's
Android-specific WSI patch reuses SDL window ID 1 for Vulkan extension
enumeration. This avoids the Android SDL restriction that only one window
can exist per Activity; the prior DXVK extension provider attempted to
create a hidden second window and failed before Vulkan instance creation.
The desktop dummy-window behavior is retained outside Android.

This result closes the standalone Android D3D9 backend smoke-test gate for
this device and this pinned community fork. It does **not** establish that
the complete The Movies D3D9 workload is supported, nor that the game host
can be built or launched on Android.

### What remains before the game can run

1. Add an Android JNI/native entry point and Gradle application around
   `tm_host_run()`; keep the Linux CLI executable and its build path intact.
2. Cross-compile the actual lifted C translation units and pcrecomp /
   win32hle runtime for Android `arm64-v8a`.
3. Validate the guest's 4 GiB address-space reservation, PE image mapping,
   relocation and import binding under Android/Bionic.
4. Pass explicit app-private game, user and system directories to the host;
   keep all original game files outside the APK and repository.
5. Link the existing guest D3D9 bridge to the tested Android DXVK library
   and exercise the real game's D3D9 resource, texture, presentation and
   fixed-function call sequence.
6. Integrate SDL/Android surface lifecycle, pause/resume, touch-to-mouse,
   audio and video; then test save/load and a full studio session.

The current `CMakeLists.txt` still describes a Linux desktop target graph:
it requires desktop pkg-config SDL2/SDL2_ttf and defines `tm_host` as an
executable. The existing Android workflows build only diagnostic APKs and
compile the game D3D9 bridge as an object-only compatibility check. No
Android shared-library target or full-game APK is claimed yet.

The Linux DXVK Native 3.1.1 pin and Linux runtime remain unchanged. The
Android test uses the pinned community adaptation
`Star1xr/dxvk-native-android` commit
`f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`; this is not upstream Android
support from DXVK.
