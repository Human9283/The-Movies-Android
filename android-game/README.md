# The Movies for Android — application

This directory contains the Android application and the native ARM64 game
target. It is separate from the Linux desktop target and the standalone
`android-d3d9-probe/` diagnostic.

## Application

- ABI: `arm64-v8a`
- Minimum SDK: 24
- Compile SDK: 36
- Target SDK: 35
- NDK: 28.2.13676358
- CMake: 3.22.1
- Activity: `GameActivity`, based on `SDLActivity`

The private CI build produces a self-contained ARM64 APK. It extracts the
user-owned Russian CD 1.1 installation from the private game-data release,
normalizes 705 MSI rows to 699 unique installed files, packages them as an
uncompressed USTAR asset, and embeds
that asset only in the private APK artifact. No game files are committed to
this public source repository.

On first launch, `SetupActivity` streams the bundled archive into
app-private `files/game/`, verifies every file against the bundled
SHA-256 manifest, checks `Movies.exe` and `Data/`, then starts the game.
The installation is performed once and reused on later launches. At least
about 2.2 GB of free internal storage is required for the extracted game
data, in addition to the space occupied by the installed APK.

A manual Storage Access Framework import remains available as a fallback
when a build has no bundled payload or the automatic installation fails.

## Native libraries

`GameActivity.getLibraries()` loads:

1. `SDL2`
2. `SDL2_ttf`
3. `dxvk_d3d9`
4. `main`

The `main` library contains the Android SDL entry point and game host.
It calls `tm_host_run()` with the app-private game, user and system paths.

## Build inputs

Gradle requires these properties:

| Property | Contents |
|---|---|
| `sdlRoot` | SDL2 source tree, Android Java sources and headers |
| `dxvkRoot` | Android DXVK Native source tree and headers |
| `pcrecompRoot` | Pinned pcrecomp source tree |
| `tmGen` | Private generated C lift directory |
| `sdlLibrary` | Android `libSDL2.so` |
| `sdl2TtfIncludeDir` | SDL2_ttf include directory |
| `sdl2TtfLibrary` | Android `libSDL2_ttf.so` |
| `dxvkLibrary` | Android `libdxvk_d3d9.so` |

The shared libraries and NDK `libc++_shared.so` must be available to the
Gradle packaging step under `app/src/main/jniLibs/arm64-v8a/`.

Example invocation (environment variables must point to the build inputs):

```sh
gradle --no-daemon -p android-game \
  -PsdlRoot="$SDL_ROOT" \
  -PdxvkRoot="$DXVK_ROOT" \
  -PpcrecompRoot="$PCRECOMP_ROOT" \
  -PtmGen="$TM_GEN" \
  -PsdlLibrary="$SDL_LIB" \
  -Psdl2TtfIncludeDir="$SDL_TTF_INCLUDE" \
  -Psdl2TtfLibrary="$SDL_TTF_LIB" \
  -PdxvkLibrary="$DXVK_LIB" \
  :app:assembleDebug
```

The Android CMake entry point is
`app/src/main/cpp/CMakeLists.txt`; it includes the shared top-level CMake
configuration. The generated lift is supplied by private CI and is not
stored in this public repository. Do not publish generated game code or
proprietary game data.

## Validation status

The standalone Android DXVK Native D3D9 diagnostic passed on a Samsung
Galaxy S24 Ultra (Snapdragon 8 Gen 3 / Adreno 750). This verifies the
tested SDL/Vulkan/D3D9 path, not the game runtime.

A full-game ARM64 APK has been built, installed and launched on a Samsung
Galaxy S24 Ultra. The game creates its D3D9 device through the Android DXVK
Native library and presents repeated animated intro frames. The current
observed run remains on the Lionhead splash and does not reach the main
menu. This confirms that the Android application, bundled-data installer,
native loader and at least part of the game's rendering path execute; it
does not establish that the game is playable.

The current development build adds a native SDL finger-event to Win32
mouse bridge and bounded input/lifecycle tracing. The bridge is intended
to translate Android touch coordinates into the mouse messages expected
by the game's Win32 input layer. Its effect on the splash-screen stall has
not yet been confirmed on-device.

Before considering the Android port functional, validate progression to
the main menu and studio, reliable touch input, pause/resume, audio, video,
save/load and long-duration stability. The splash-screen stall and
occasional surface-orientation changes remain open issues.

See [Android feasibility](../docs/ANDROID-FEASIBILITY.md) and
[Android port architecture](../docs/ANDROID-PORT-AUDIT.md) for measured
results and the remaining validation work.
