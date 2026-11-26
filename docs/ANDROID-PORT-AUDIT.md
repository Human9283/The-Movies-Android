# Android ARM64 port: architecture and validation

## 1. Target

The target is a native Android application for the Russian CD release of
The Movies 1.1. The original Windows executable is statically recompiled
to C; the application does not run the Windows executable through an x86
emulator or Wine.

Supported Android ABI: `arm64-v8a`.

The Android application is implemented in `android-game/`. The full
lifted game has not yet been verified in an APK or launched on Android.

## 2. Runtime architecture

```text
Android launcher
  └─ SetupActivity
       ├─ Storage Access Framework folder picker
       ├─ validate Movies.exe and Data/
       └─ copy installation to app-private files/game/
            └─ GameActivity : SDLActivity
                 └─ SDL2 loads native libraries
                      ├─ SDL2
                      ├─ SDL2_ttf
                      ├─ dxvk_d3d9
                      └─ main
                           └─ SDL_main (tm_host_android.c)
                                └─ tm_host_run()
                                     ├─ pcrecomp / win32hle
                                     ├─ lifted Movies.exe 1.1 C
                                     ├─ host Win32 compatibility layer
                                     └─ guest D3D9 bridge
                                          └─ DXVK Native D3D9
                                               └─ Vulkan
```

The Android entry point obtains the SDL app-private storage path and uses
these directories:

| Path | Purpose |
|---|---|
| `files/game/` | Imported game installation |
| `files/user/` | User-writable game files and saves |
| `files/system/` | Synthetic Windows system root |

The APK contains application code and native libraries only. It does not
contain the game executable, game data, disc images or generated lifted C.

## 3. Game-data import

`SetupActivity` uses `ACTION_OPEN_DOCUMENT_TREE` to let the user select an
already installed game directory. It recursively copies the selected tree
into `files/game-import/`, validates `Movies.exe` and `Data/`, then
renames the staging directory to `files/game/`.

If a previous installation exists, it is renamed to a backup before the
staged copy is activated. On activation failure, the previous directory is
restored. The importer does not install the game from ISO images and does
not download game files.

The native entry point repeats the `Movies.exe` and `Data/` checks before
calling the host runtime.

## 4. Build configuration

The Gradle application configures:

| Setting | Value |
|---|---|
| Namespace / application ID | `com.human9283.themovies` |
| Compile SDK | 36 |
| Target SDK | 35 |
| Minimum SDK | 24 |
| NDK | 28.2.13676358 |
| CMake | 3.22.1 |
| ABI filter | `arm64-v8a` |
| C++ runtime | `c++_shared` |

The Android CMake entry point is
`android-game/app/src/main/cpp/CMakeLists.txt`. It includes the shared
top-level CMake configuration. The Android target builds the native
application library `main` and links the host, pcrecomp/win32hle runtime,
lifted C translation units, SDL2, SDL2_ttf and the Android DXVK Native
D3D9 library.

The generated lift is supplied to private CI as `TM_GEN`; it is not
committed to this public repository.

Build inputs are passed as Gradle properties:

- `sdlRoot`
- `dxvkRoot`
- `pcrecompRoot`
- `tmGen`
- `sdlLibrary`
- `sdl2TtfIncludeDir`
- `sdl2TtfLibrary`
- `dxvkLibrary`

See [android-game/README.md](../android-game/README.md) for the build
command.

## 5. Native dependencies

| Dependency | Pinned version / revision | Role |
|---|---|---|
| SDL2 | 2.30.9 | Android activity, window, event and input integration |
| SDL2_ttf | Android ARM64 build used by the application | SDL text rendering dependency |
| DXVK Native Android fork | `Star1xr/dxvk-native-android` commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` | D3D9 implementation over Vulkan |
| DXVK Native base | `native-1.9.2b`, base commit `c8dc91fabd00cac11d697ccf07426e798393cd40` | Upstream base of the Android fork |
| Android NDK | 28.2.13676358 | ARM64 compilation and shared C++ runtime |

The Android DXVK dependency is a community fork. It is not an Android
release supported by upstream DXVK. Its licence and notices must accompany
redistribution. The Linux desktop build remains pinned to DXVK Native
3.1.1; the Android dependency does not change the Linux build.

References: [SDL Android build documentation](https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md),
[Android NDK/CMake documentation](https://developer.android.com/studio/projects/install-ndk),
[DXVK driver support](https://github.com/doitsujin/dxvk/wiki/Driver-support).

## 6. Guest address space

The game is 32-bit, while the Android process is AArch64. Guest addresses
are represented inside a reserved 4 GiB guest-memory region; host pointers
must not be exposed as guest pointers. The pcrecomp based-memory model
translates guest virtual addresses relative to the host mapping base.

The Android application build does not by itself validate this memory
model on a device. The following operations still require on-device
verification:

- reserving the 4 GiB guest address space;
- mapping the PE image and applying relocations;
- mapping guest heaps and runtime allocations;
- binding imported Win32 functions to host shims.

## 7. Graphics backend

The game's guest D3D9 interfaces are translated by the host bridge in
`src/host/tm_d3d9_dxvk.cpp` and related C shims. The Android build links
`libdxvk_d3d9.so` from the pinned Android fork.

A separate diagnostic application has passed on a Samsung Galaxy S24 Ultra
(Snapdragon 8 Gen 3, Adreno 750). Its on-device report confirms:

- SDL initialization and Android Vulkan surface creation;
- loading the Android DXVK Native 1.9.2 library;
- `Direct3DCreate9` and `IDirect3D9::CreateDevice`;
- fixed-function clear/draw and `Present`.

The test uses a diagnostic triangle and does not execute game code. It
does not prove compatibility with every D3D9 operation or resource format
used by The Movies.

The stock Adreno driver does not expose all extensions required by the
Linux DXVK 3.1.1 build. The Android application therefore uses the separate
Android DXVK Native fork and its mobile-specific implementation. Do not
infer Android compatibility from the Linux backend version.

## 8. Lift status

The private CD 1.1 lift currently records:

| Metric | Value |
|---|---:|
| Lifted entries | 85,910 |
| Generated C translation units | 215 |
| Lift exceptions | 0 |
| Not-lifted stubs | 0 |
| Extent walks without recognized terminator | 559 |
| Unresolved indirect-tail targets | 7 |

The last two metrics describe unresolved analysis cases. Zero lift
exceptions does not establish semantic correctness or runtime coverage.

## 9. Build and runtime status

The full-game ARM64 build is executed in private CI so generated C and
game data remain private:
[workflow run 45809671909](https://github.com/Human9283/the-movies-gamedata/actions/runs/45809671909).

At the latest status check (2026-11-26), attempt 3 was still running.
No successful full-game APK is claimed.

The standalone D3D9 diagnostic passed on the S24 Ultra:
[workflow run 45793972336](https://github.com/Human9283/The-Movies-Android/actions/runs/45793972336).

The full game still requires these runtime checks:

1. successful full-game APK packaging and native-library inspection;
2. application startup and guest-memory reservation;
3. PE mapping, relocation and import binding;
4. execution through CRT initialization into `WinMain`;
5. creation of the game's D3D9 device;
6. first frame rendered by the game's own code;
7. menu and studio-lot operation;
8. touch input, Activity pause/resume, audio, video and save/load.

A diagnostic D3D9 pass or a successful APK build must not be reported as
a successful game launch.
