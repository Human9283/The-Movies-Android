# Third-party notices

The Android build guide uses the following separately obtained third-party projects. Their licences apply to the corresponding source and binaries. This repository does not vendor their source trees or prebuilt libraries.

| Component | Source | Licence |
|---|---|---|
| pcrecomp / win32hle runtime | https://github.com/sp00nznet/pcrecomp | MIT |
| The Movies lift driver | https://github.com/sp00nznet/themovies | MIT |
| SDL2 | https://github.com/libsdl-org/SDL | zlib |
| SDL2_ttf | https://github.com/libsdl-org/SDL_ttf | zlib |
| Android DXVK Native fork | https://github.com/Star1xr/dxvk-native-android | zlib |

The repository contains patches for selected pcrecomp/win32hle files and a virtual-key to DirectInput scan-code table derived from win32hle. The pcrecomp MIT licence applies to those derived portions. The build guide pins external revisions where required.

Before redistributing an APK, review the licences and notices for the exact revisions and build configuration used, and include the notices required by those licences. The original game is proprietary and is not covered by this project's MIT licence.
