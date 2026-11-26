# The Movies for Android — ARM64 application

This directory contains the native Android application project. It is
separate from the Linux desktop target and the diagnostic
`android-d3d9-probe/` application.

## Application

- ABI: `arm64-v8a`
- Minimum SDK: 24
- Compile SDK: 36
- Target SDK: 35
- NDK: 28.2.13676358
- CMake: 3.22.1
- SDL activity: `GameActivity`, subclass of `SDLActivity`

`SetupActivity` imports an already installed game directory through the
Android Storage Access Framework. The selected directory must contain
`Movies.exe` and `Data/`. Files are copied to app-private
`files/game/`; user-writable files and the synthetic Windows system root
are stored separately in `files/user/` and `files/system/`.

The importer stages the copy, validates the required files and then
activates it. It does not install from disc images, download game data or
package copyrighted game files in the APK.

## Native library loading

`GameActivity.getLibraries()` loads the following libraries in order:

1. `SDL2`
2. `SDL2_ttf`
3. `dxvk_d3d9`
4. `main`

The final library contains the Android SDL entry point and the game host.
It calls `tm_host_run()` with the app-private game, user and system paths.

## Native build inputs

Gradle requires these properties:

| Property | Contents |
|---|---|
| `sdlRoot` | SDL2 source tree, including Android Java sources and headers |
| `dxvkRoot` | Android DXVK Native source tree and headers |
| `pcrecompRoot` | Pinned pcrecomp source tree |
| `tmGen` | Private generated C lift directory |
| `sdlLibrary` | Built Android `libSDL2.so` |
| `sdl2TtfIncludeDir` | SDL2_ttf include directory |
| `sdl2TtfLibrary` | Built Android `libSDL2_ttf.so` |
| `dxvkLibrary` | Built Android `libdxvk_d3d9.so` |

The corresponding shared libraries and NDK `libc++_shared.so` must be
available to the Gradle packaging step under
`app/src/main/jniLibs/arm64-v8a/`.

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
`app/src/main/cpp/CMakeLists.txt`; it includes the repository's shared
top-level CMake configuration. The generated lift is supplied by private
CI and is not stored in this public repository.

For the first ARM64 full-link experiment, private CI splits generated C
translation units at function boundaries into chunks no larger than 3 MiB
and checks that functions and symbols are preserved. The large dispatch
table is emitted as AArch64 assembly rather than compiled from C; a C
static-assert file checks the 64-bit pointer ABI and the 16-byte dispatch
entry layout. Generated C chunks are compiled with `-O0 -g0`, and Ninja
compilation is limited to one job to reduce peak memory. These are temporary
build-memory measures pending a reproducible full APK build.

## Validation status

The standalone Android DXVK Native D3D9 diagnostic passed on a Samsung
Galaxy S24 Ultra (Snapdragon 8 Gen 3 / Adreno 750). It validates the tested
SDL/Vulkan/D3D9 backend path, not the game runtime.

The full-game ARM64 build runs in private CI so generated game code and
assets are not published:
[latest experiment](https://github.com/Human9283/the-movies-gamedata/actions/runs/45887244868),
[preceding experiment](https://github.com/Human9283/the-movies-gamedata/actions/runs/45885363769).
At the documentation update on 2026-11-26, both runs had been observed in
progress. The newer run had completed assembly dispatch generation, source
splitting and dependency/toolchain setup; successful native linking, APK
verification and artifact upload were not yet confirmed. A successful
full-game APK has not yet been verified. See the workflow pages for live
status.

After the APK build, runtime validation must cover guest-memory reservation,
PE mapping and relocation, import binding, CRT initialization, `WinMain`,
the game's D3D9 device and first rendered frame. Menu/studio operation,
touch input, pause/resume, audio, video and save/load remain unverified.
