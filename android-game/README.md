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

`SetupActivity` imports an existing game installation through the
Android Storage Access Framework. The selected directory must contain
`Movies.exe` and `Data/`. Files are copied into app-private
`files/game/`; writable game files and the synthetic Windows system root
use `files/user/` and `files/system/`.

The importer stages and validates the copy before activating it. It does
not install from disc images, download game data or include copyrighted
game files in the APK.

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

The complete game's ARM64 APK and on-device launch have not yet been
verified. Before considering the Android port functional, validate
application startup, guest-memory reservation, PE mapping and relocation,
import binding, entry into `WinMain`, the game's D3D9 device and first
frame. Then test menu and studio operation, touch input, pause/resume,
audio, video and save/load.

See [Android feasibility](../docs/ANDROID-FEASIBILITY.md) and
[Android port architecture](../docs/ANDROID-PORT-AUDIT.md) for measured
results and the remaining validation work.
