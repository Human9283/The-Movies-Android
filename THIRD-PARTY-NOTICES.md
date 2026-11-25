# Third-party notices

This repository contains material derived from the projects below. Their
licences apply to that material.

## pcrecomp and its win32hle runtime

Source: https://github.com/sp00nznet/pcrecomp (MIT).

Derived material in this repository:
- `patches/pcrecomp/*.patch`: changes to win32hle files, with the lines of
  those files they change;
- the virtual-key to DirectInput scan-code table in
  `src/host/tm_dinput8.c`, from win32hle's `dinput.c`.

The build also compiles pcrecomp's runtime from a separate checkout; it is
not part of this repository.

```
MIT License

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
```

## Not included

- DXVK (zlib licence) is built from its own checkout and linked as a shared
  library; no DXVK source is in this repository
  ([docs/DXVK-INTEGRATION.md](docs/DXVK-INTEGRATION.md) 1).
- [sp00nznet/themovies](https://github.com/sp00nznet/themovies) (MIT) is
  cited for its published findings
  ([docs/EXTERNAL-REFERENCES.md](docs/EXTERNAL-REFERENCES.md)); no code
  from it is copied here.
