# Third-party notices

The APK contains these third-party components:

- **DXVK Native Android fork** — [Star1xr/dxvk-native-android](https://github.com/Star1xr/dxvk-native-android), pinned commit `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552`, based on DXVK Native `native-1.9.2b`. License: zlib/libpng. The workflow copies the repository's `LICENSE` into the APK at `assets/third_party/DXVK-LICENSE.txt`.
- **SDL2** — [libsdl-org/SDL](https://github.com/libsdl-org/SDL), tag `release-2.30.9`. License: zlib. The workflow copies `LICENSE.txt` into the APK at `assets/third_party/SDL2-LICENSE.txt`.

The Android build also fetches pcrecomp revision `7c07a0b42059b6dc4313d9b4ebded6f7a72db8f3) for the compile-only compatibility check of the project's D3D9 host bridge. pcrecomp is not linked into or packaged with this diagnostic APK.

The DXVK D3D9 and SDL2 shared libraries are built from the pinned sources in GitHub Actions; they are not checked into this repository. The app's own test harness is in `app/src/main/cpp/main.cpp`.
