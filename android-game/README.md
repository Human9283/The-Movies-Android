# Android game host (ARM64)

This is the first full-link target for the Android host. It is deliberately
separate from `android-d3d9-probe/`: the probe remains a diagnostic APK and
the Linux desktop build remains unchanged.

## Inputs

- `PCRECOMP_SOURCE_DIR`: the pinned pcrecomp checkout used to generate the lift.
- `TM_GEN`: private `run_lift.py` output containing `recomp_*.c`,
  `recomp_dispatch.c` and `recomp_funcs.h`.
- `SDL2_ROOT`: Android SDL2 build with `include/SDL.h` and
  `lib/arm64-v8a/libSDL2.so`.
- `DXVK_ROOT`: Android DXVK Native build with headers and
  `lib/arm64-v8a/libdxvk_d3d9.so`.

Configure with the Android NDK CMake toolchain, `ANDROID_ABI=arm64-v8a`,
and `ANDROID_STL=c++_shared`. The resulting shared library is
`libmain.so`, loaded by SDL's Android activity glue. The app must package
SDL's Java activity sources/resources and the matching SDL2, DXVK and
libc++ shared libraries.

This is a build-system milestone, not a claim that the target already links
or runs. The first build will likely expose unresolved Bionic/NDK assumptions
or lifted-code compiler issues. After a successful link, validate guest
memory reservation and PE mapping on-device before attempting game startup.

The host entry point expects the user's own installed game files under
`<SDL internal files>/game/Movies.exe` and `Data/`; saves and the synthetic
Windows system directory are kept in sibling app-private folders. No
copyrighted game data or generated proprietary lift is stored in this repo.
