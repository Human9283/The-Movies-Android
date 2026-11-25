# Android ARM64: what is known, what is not

This document separates what was read or measured from what is assumed,
for running the port on Android ARM64 with DXVK Native as the Direct3D 9
backend ([DXVK-INTEGRATION.md](DXVK-INTEGRATION.md)). Nothing here has run
on an Android device yet.

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

There is no feature table here to copy, and no claim about a named GPU.
The test on a device is DXVK itself.

1. Build the proof of concept (`dxvk_present`) for Android ARM64 (section 4)
   and run it on the device with `DXVK_LOG_LEVEL=info`.
2. DXVK logs every adapter it finds. For an adapter it rejects, it logs
   the first missing requirement, from the same code that decides it.
3. That log, the device model, and the driver name and version DXVK prints,
   go into this document as a [run] entry. A capability claim is made only
   for a device with such an entry.

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
