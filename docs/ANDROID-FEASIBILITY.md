# Android ARM64 feasibility

## Verified result

A standalone Android D3D9 diagnostic passed on a Samsung Galaxy S24 Ultra
(Snapdragon 8 Gen 3, Adreno 750). It exercised SDL initialization, an Android
Vulkan surface, Android DXVK Native loading, D3D9 device creation, a
fixed-function draw and `Present`.

This is a graphics diagnostic, not the game. The full ARM64 game APK has
also been installed and launched on the same Galaxy S24 Ultra. The game
creates its D3D9 device and repeatedly presents animated intro frames, but
the observed run remains on the Lionhead splash and has not reached the
main menu. The Android runtime is partially validated, not yet playable.

[Diagnostic workflow](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).

## Backend

The Android diagnostic and port use the community fork
[`Star1xr/dxvk-native-android`](https://github.com/Star1xr/dxvk-native-android),
pinned at `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`, based on DXVK Native
`native-1.9.2b` commit `c8dc91fabd00cac11d697ccf07426e798393cd40`.

This is not an upstream Android release. The Linux desktop backend is
separate and uses DXVK Native 3.1.1. Do not infer compatibility between the
two backends.

## Android application status

The Android application and ARM64 game target are now integrated into the
source tree. The application uses SDLActivity, an ARM64 native library and
the shared host runtime. A full-game APK with the user's Russian CD 1.1
data bundled by private CI has been installed and launched on a Galaxy S24
Ultra. On first launch, the bundled archive is extracted and verified in
app-private storage; subsequent update APKs can omit the game-data payload.

The APK does not contain the original executable, proprietary game data or
generated lifted C. Generated game code is supplied by private CI.

## Validation still required

A successful standalone diagnostic does not validate the full game. The
following stages remain to be demonstrated on a physical Android device:

1. Full native game-library link and APK packaging.
2. Application installation and startup.
3. Reservation of the 4 GiB guest address space.
4. PE image mapping, relocations and import binding.
5. Runtime initialization through the game's `WinMain`.
6. Creation of the game's D3D9 device.
7. First frame rendered by the game's own code.
8. Main menu and studio-lot operation.
9. Touch input, Activity pause/resume, audio, video and save/load.

Report build, package, install and runtime results as separate stages.
An APK build is not evidence of a successful game launch.
