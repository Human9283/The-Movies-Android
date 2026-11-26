# The Movies for Android — ARM64 application target

This is a separate Android application project. It does not alter the desktop CMake target or the diagnostic android-d3d9-probe APK.

## What is implemented

- SDL2 Android activity integration.
- A first-run Storage Access Framework folder picker.
- Recursive copy of the user's installed game folder into app-private storage.
- Validation of Movies.exe and Data/ before launch.
- A dedicated NDK CMake target that links the real host, pcrecomp/win32hle, generated lifted C, SDL2 and Android DXVK Native D3D9 into libmain.so.

The importer expects an already installed game directory, not the three-disc ISO archive. It copies the directory selected by the user; it does not download, extract, or package copyrighted game files.

## Build inputs

| Property | Meaning |
|---|---|
| sdlRoot | SDL2 source tree (Android Java sources and headers) |
| dxvkRoot | Android DXVK Native source tree (headers) |
| pcrecompRoot | Pinned pcrecomp source tree |
| tmGen | Private generated lift directory |
| sdlLibrary | Built Android libSDL2.so |
| dxvkLibrary | Built Android libdxvk_d3d9.so |

The matching libSDL2.so, libdxvk_d3d9.so and NDK libc++_shared.so must be copied to app/src/main/jniLibs/arm64-v8a/ before Gradle packaging.

Example (paths are illustrative):

```sh
gradle --no-daemon -p android-game \
  -PsdlRoot="$SDL_ROOT" \
  -PdxvkRoot="$DXVK_ROOT" \
  -PpcrecompRoot="$PCRECOMP_ROOT" \
  -PtmGen="$TM_GEN" \
  -PsdlLibrary="$SDL_LIB" \
  -PdxvkLibrary="$DXVK_LIB" \
  :app:assembleDebug
```

## Current status and validation gates

The Android application shell and full-link CMake target are present, but the target has not yet been built against the generated lift. The first successful configure/build is the next gate. The APK must then be tested on the Galaxy S24 Ultra for:

1. 4 GiB guest-memory reservation and PE mapping;
2. entry into the lifted CRT and WinMain;
3. first actual game frame through the tested DXVK Native D3D9 path;
4. touch input, pause/resume, sound/video and save/load.

Do not treat the D3D9 probe result as evidence that the original game runs.