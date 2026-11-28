# Third-party notices

The Android build downloads third-party source at the revisions listed in
[docs/ANDROID-BUILD.md](docs/ANDROID-BUILD.md). No third-party dependency
source tree or prebuilt dependency library is vendored in this repository.

## Components used by the Android build

| Component | Pinned source | Licence and packaging |
|---|---|---|
| SDL2 | `release-2.32.10` | zlib; packaged as `libSDL2.so` |
| SDL2_ttf | `release-2.24.0` | zlib; packaged as `libSDL2_ttf.so` |
| FreeType | SDL2_ttf submodule `VER-2-13-2-SDL` (`12c5e620858bd503731091e9371d06c0a3e7c967`) | FreeType Project Licence (FTL); built into SDL2_ttf |
| DXVK Native Android fork | `f2c7207b3d8bc848fe3a27cef34ee5e67aeec552` | zlib/libpng; packaged as `libdxvk_d3d9.so` |
| pcrecomp / win32hle | `5f1d2f76873419bf0414de7a0a9aca34d8e720f9` | MIT; build-time runtime and source of the patches/table noted below |
| The Movies lift driver | `8e278f6b22d70095d0b1b10803370613b20862f5` | MIT for the driver's own files; build-time tool only |

The SDL2_ttf build in the guide enables its vendored FreeType dependency and
disables HarfBuzz. FreeType is statically linked into the SDL2_ttf shared
library. The FreeType licence therefore applies to APKs containing that
library.

This repository contains:
- patches to selected pcrecomp/win32hle files under `patches/pcrecomp/`;
- a virtual-key to DirectInput scan-code table derived from win32hle in
  `src/host/tm_dinput8.c`.

The original game and the generated lift are not covered by the MIT licence
in this repository. The external lift driver explicitly distinguishes its
own MIT-licensed tools from generated code derived from the proprietary game
executable. Do not distribute the generated lift or original game data.

## SDL2 and SDL2_ttf — zlib licence

Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

## DXVK Native Android fork — zlib/libpng licence

Copyright (c) 2017-2021 Philip Rebohle
Copyright (c) 2019-2021 Joshua Ashton

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

- The origin of this software must not be misrepresented; you must not
  claim that you wrote the original software. If you use this software
  in a product, an acknowledgment in the product documentation would be
  appreciated but is not required.
- Altered source versions must be plainly marked as such, and must not be
  misrepresented as being the original software.
- This notice may not be removed or altered from any source distribution.

## FreeType Project Licence (FTL)

Portions of this software are copyright © 1996-2002, 2006 The FreeType
Project (www.freetype.org). All rights reserved.

The FreeType Project is distributed under the FreeType Project Licence
(FTL), a permissive licence allowing use, modification, redistribution
and sublicensing, including in commercial products, subject to its terms.

Redistribution of source code must retain the FTL licence file unaltered.
Redistribution in binary form must provide a disclaimer stating that the
software is based in part on the work of the FreeType Team in the
distribution documentation. The project also asks that its use be
acknowledged. The full licence is available at:
https://gitlab.freedesktop.org/freetype/freetype/-/blob/VER-2-13-2-SDL/docs/FTL.TXT

## pcrecomp / win32hle and The Movies lift driver — MIT licence

Copyright (c) 2026 sp00nz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

The MIT notice above applies to the upstream projects' own source and to the
pcrecomp-derived material identified in this file. It does not license
The Movies, its executable, game data, or generated code lifted from it.
