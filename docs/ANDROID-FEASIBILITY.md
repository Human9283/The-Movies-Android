# Android ARM64 feasibility

## Summary

The project has an Android application shell and a native `arm64-v8a`
build target. A separate diagnostic application has successfully exercised
the SDL2, Vulkan and Android DXVK Native D3D9 path on a Samsung Galaxy S24
Ultra (Snapdragon 8 Gen 3, Adreno 750).

**The complete game has not yet been verified in an Android APK or launched
on a device.** The diagnostic application does not execute game code.

## Application target

The application project is in [`android-game/`](../android-game/README.md).
It uses SDLActivity and an Android native library named `libmain.so`.
The current setup flow imports an already installed game directory through
the Android Storage Access Framework and validates `Movies.exe` and
`Data/`. Game data and generated lift files are not stored in this public
repository or included in the diagnostic APK.

The Android target uses the shared host runtime, pcrecomp/win32hle,
generated C from the supported CD 1.1 executable, SDL2 and an Android
DXVK Native D3D9 backend. Its generated lift is supplied by private CI.

## Device and graphics results

Test device:

- Samsung Galaxy S24 Ultra
- Snapdragon 8 Gen 3
- Adreno 750
- Android ABI: `arm64-v8a`

The separate D3D9 diagnostic confirmed SDL initialization, creation of an
Android Vulkan surface, loading of the Android DXVK Native library,
`Direct3DCreate9`, `IDirect3D9::CreateDevice`, a fixed-function draw and
`Present`.

[Diagnostic workflow run](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).

This proves the tested graphics path on this device only. It does not
prove that all D3D9 features used by The Movies are supported.

The Android target uses the community-maintained
[`Star1xr/dxvk-native-android`](https://github.com/Star1xr/dxvk-native-android)
fork (based on DXVK Native 1.9.2b). It is separate from the Linux desktop
build, which uses DXVK Native 3.1.1. The Android fork is not an
upstream-supported Android release; retain its licence and notices when
redistributing it.

## Build status

The full-game ARM64 build runs in private CI. Generated game code and
proprietary game data are kept out of the public repository.

The build is still experimental. The private workflow applies source
generation and compiler-memory workarounds to handle the large lifted
program. These are implementation details of the current CI pipeline and
may change; the workflow is the source of truth for live build status.

No successful full-game APK is claimed until the native library links,
the APK passes packaging checks and the artifact is verified.

## Remaining validation

After a full APK is produced, the complete game still needs to be tested
on an Android device:

1. Application installation and startup.
2. Guest 32-bit address-space reservation.
3. PE image mapping, relocations and imported-function binding.
4. Runtime initialization through the game's `WinMain`.
5. Creation of the game's D3D9 device.
6. First frame rendered by the game's own code.
7. Main menu and studio-lot operation.
8. Touch input, pause/resume, audio, video and save/load.

A successful APK build is evidence of compilation and packaging, not
evidence of a successful game launch.