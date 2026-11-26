# Android ARM64 port: architecture and validation

## Target

The target is a native Android application for the Russian CD edition of
The Movies 1.1. The original Windows executable is statically recompiled
to C; it is not run through an x86 emulator or Wine.

Supported ABI: `arm64-v8a`.

The Android application is in [`android-game/`](../android-game/README.md).
A full ARM64 APK has been built, installed and launched on a Samsung
Galaxy S24 Ultra. The game reaches D3D9 device creation and repeatedly
presents animated intro frames, but the observed run remains on the
Lionhead splash and has not reached the main menu. The Android runtime is
therefore partially validated and still under active debugging.

## Runtime architecture

```text
Android launcher
  └─ SetupActivity
       ├─ Select an installed game directory (Storage Access Framework)
       ├─ Validate Movies.exe and Data/
       └─ Copy to app-private files/game/
            └─ GameActivity (SDLActivity)
                 └─ SDL2 loads native libraries
                      ├─ SDL2 / SDL2_ttf
                      ├─ dxvk_d3d9
                      └─ main
                           └─ SDL_main
                                └─ tm_host_run()
                                     ├─ pcrecomp / win32hle
                                     ├─ lifted game code
                                     ├─ Win32 compatibility shims
                                     └─ D3D9 host bridge
                                          └─ Android DXVK Native
                                               └─ Vulkan
```

The application uses separate app-private locations:

| Path | Purpose |
|---|---|
| `files/game/` | Imported game installation |
| `files/user/` | Writable game files and saves |
| `files/system/` | Synthetic Windows system root |

The APK does not contain the original executable, game data, disc images
or generated lifted C.

## Game-data import

`SetupActivity` uses `ACTION_OPEN_DOCUMENT_TREE` to select an existing
installed game directory. It copies the selected tree to a staging
directory, validates `Movies.exe` and `Data/`, and then activates it as
`files/game/`.

An existing installation is backed up before activation and restored if
activation fails. The importer does not install from ISO images or download
game files. The native entry point repeats the required-file checks before
starting the host runtime.

## Build configuration

| Setting | Value |
|---|---|
| Application ID | `com.human9283.themovies` |
| Compile SDK | 36 |
| Target SDK | 35 |
| Minimum SDK | 24 |
| Android NDK | 28.2.13676358 |
| CMake | 3.22.1 |
| ABI | `arm64-v8a` |
| C++ runtime | `c++_shared` |

The Android CMake entry point is
`android-game/app/src/main/cpp/CMakeLists.txt`. The native target links
the shared host, pcrecomp/win32hle, the generated CD 1.1 lift, SDL2,
SDL2_ttf and the Android DXVK Native D3D9 library.

The generated lift is provided to private CI. It is not committed to this
public repository. The Android build uses an AArch64 dispatch table and
a compile-time layout check for its 64-bit function pointers. Details of
the source-generation and compiler-memory strategy are intentionally kept
in the private build workflow rather than treated as public runtime
architecture.

Build properties and the Gradle command are documented in
[android-game/README.md](../android-game/README.md).

## Native dependencies

| Dependency | Version / revision | Role |
|---|---|---|
| SDL2 | 2.30.9 | Android activity, window and input integration |
| SDL2_ttf | Android ARM64 build used by the application | Text rendering |
| Android DXVK Native | `Star1xr/dxvk-native-android`, commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` | D3D9 over Vulkan |
| DXVK Native base | `native-1.9.2b`, commit `c8dc91fabd00cac11d697ccf07426e798393cd40` | Android fork base |
| Android NDK | 28.2.13676358 | ARM64 compilation |

The Android DXVK dependency is a community fork, not an upstream-supported
Android release. Preserve its licence and notices when redistributing it.
The Linux desktop target remains on DXVK Native 3.1.1.

References: [SDL Android documentation](https://github.com/libsdl-org/SDL/blob/main/docs/README-android.md),
[Android NDK documentation](https://developer.android.com/studio/projects/install-ndk),
[DXVK driver support](https://github.com/doitsujin/dxvk/wiki/Driver-support).

## Guest address space

The game is 32-bit and the Android process is AArch64. Guest addresses
must remain distinct from host pointers and are translated relative to the
host mapping base by the pcrecomp memory model.

A successful compile does not validate the runtime memory model. The
4 GiB guest address-space reservation, PE mapping, relocations, heap
mapping and import binding still require device testing.

## Graphics validation

A separate diagnostic application passed on a Samsung Galaxy S24 Ultra
(Snapdragon 8 Gen 3, Adreno 750). It verified SDL initialization, Android
Vulkan surface creation, loading Android DXVK Native 1.9.2, D3D9 device
creation, fixed-function drawing and `Present`.

The full-game APK has also been run on this device. Native logs show
successful DXVK/Vulkan initialization, D3D9 device creation, game rendering
calls and repeated `Present` operations. Captured frames show the animated
Lionhead intro logo and particles. This is stronger evidence than the
standalone diagnostic, but it only proves that the game reaches its intro
render loop. It does not prove that the main menu or gameplay works.

The diagnostic renders a test primitive and does not execute game code.
It does not establish compatibility with every D3D9 operation or resource
format used by The Movies.

The stock Adreno driver does not expose all extensions required by the
Linux DXVK 3.1.1 build. The Android target therefore uses the separate
Android DXVK Native fork. Do not infer Android compatibility from the
Linux backend version.

## Lift coverage and known analysis limits

The private CD 1.1 lift records 85,910 lifted entries, 215 generated C
translation units, zero lift exceptions and zero not-lifted stubs.
Analysis also reports 559 extent walks without a recognized terminator
and 7 unresolved indirect-tail targets. These are analysis limitations;
zero lift exceptions does not establish semantic correctness or complete
runtime coverage.

## Validation still required

The following stages have been observed on Android: APK packaging,
installation, application startup, native library loading, D3D9 device
creation and repeated game-frame presentation. The remaining validation
sequence is:

1. Confirm guest-memory reservation, PE mapping, relocation and import
   binding from the device logs.
2. Progress beyond the animated Lionhead splash to the main menu.
3. Verify touch input and stable landscape surface geometry.
4. Enter and operate the studio lot.
5. Test Activity pause/resume, audio, video, save/load and long sessions.

A successful diagnostic D3D9 test or APK build must not be described as
a successful game launch.
