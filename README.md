# The Movies for Android

A native Android ARM64 port of Lionhead Studios' **The Movies** (2005), built
by statically recompiling the original Windows executable to C and replacing
its Windows and DirectX dependencies with portable implementations.

This is not a remake or a clone. The game's own code runs, translated
instruction by instruction, with its original data. You need your own copy
of the game.

> **Status: early bring-up.** The game does not run yet. On 64-bit Linux
> the recompiled start-up code reaches `WinMain` and runs the game's own
> start-up, past its DirectShow start-up probe, as far as its own Direct3D 9
> `CreateDevice` call. That call is recorded, but no device exists yet; see
> [Status](#status).

## Contents

- [How it works](#how-it-works)
- [Status](#status)
- [Requirements](#requirements)
- [Getting started](#getting-started)
- [Project layout](#project-layout)
- [Documentation](#documentation)
- [License](#license)
- [Credits](#credits)

## How it works

1. **Install** the game data from the original discs without Windows
   (`tools/tm_install.py` reads the Windows Installer database and its
   cabinets).
2. **Analyse and lift** `Movies.exe` with the
   [pcrecomp](https://github.com/sp00nznet/pcrecomp) toolchain: function
   catalog, C++ RTTI, and a lift of every function to C.
3. **Host** the lifted code on pcrecomp's `win32hle`, a Win32 implementation
   on POSIX and SDL2, in a 64-bit process with the game's 32-bit address space
   mapped as one block.
4. **Replace** what the game loads at run time (Direct3D 9, DirectInput 8,
   DirectShow video, DirectSound/OpenAL) with portable backends, then package
   the result as an Android app.

The supported game build is the Russian CD edition, version 1.1
(`Movies.exe`, SHA-1 `(hash removed)`). Other
builds need their own lift.

## Status

```
Progress  ███████░░░░░░░░░░░░░  3 of 8 milestones (37%)
```

The bar counts finished milestones, M0 to M7 ([docs/AUDIT.md](docs/AUDIT.md) 6),
equally. They are not equal in work: Direct3D 9 (M4) and video, audio and
input (M5) are the largest. M3 is in progress and is not counted.

| Milestone | State |
|---|---|
| M0: install game data from the discs on Linux | done: 705/705 files |
| M1: function catalog and lift of `Movies.exe` 1.1 | done: 85,910 functions, 0 lift errors, 7 unresolvable ITAIL labels |
| M2: lifted start-up code reaches `WinMain` | done on Linux x86-64; arm64 and Android not yet run |
| M3: run to the game's own Direct3D 9 device initialisation (no rendering) | in progress, stopped at the game's `CreateDevice` (recorded, no device yet): [docs/M3.md](docs/M3.md), [docs/D3D9-REQUIREMENTS.md](docs/D3D9-REQUIREMENTS.md). Backend chosen: DXVK Native; its proof of concept creates and presents the game's device request on software Vulkan ([docs/DXVK-INTEGRATION.md](docs/DXVK-INTEGRATION.md)) |
| M4: the first genuine frame from the game's rendering code | planned |
| Later: video, audio, input, Android packaging | planned |

Measured results and open problems: [docs/M2.md](docs/M2.md),
[docs/AUDIT.md](docs/AUDIT.md) and
[docs/REPOSITORY-AUDIT.md](docs/REPOSITORY-AUDIT.md).

## Requirements

- Linux x86-64 or arm64
- Python 3.10+ with `pefile`, `capstone`, `unicorn`, `pycdlib`
- `msitools`, `cabextract`
- CMake 3.20+, Ninja, Clang or GCC, SDL2 and SDL2_ttf development packages
- Optional, for the Direct3D 9 backend: DXVK Native v3.1.1 built with meson, and a Vulkan 1.3 driver ([docs/BUILD.md](docs/BUILD.md) 5.1)
- A checkout of [pcrecomp](https://github.com/sp00nznet/pcrecomp) and of
  [themovies](https://github.com/sp00nznet/themovies) (for its lift driver)
- The three disc images of The Movies (CD edition)

## Getting started

Install the game data from your discs:

```bash
python3 tools/tm_install.py --iso ~/iso --out ~/tm/game
```

Then follow [docs/BUILD.md](docs/BUILD.md) to build the function catalog,
lift the executable and build the host. Run it with:

```bash
build/tm_host ~/tm/game --stop-at-winmain --watchdog 120
```

Run the tests:

```bash
python3 -m unittest tools/test_tm_install.py
```

```bash
python3 tests/m2_winmain.py --host ~/tm/build/tm_host --game ~/tm/game
```

```bash
python3 tests/m3_boundary.py --host ~/tm/build/tm_host --game ~/tm/game
```

```bash
ctest --test-dir build --output-on-failure
```

## Project layout

```
tools/tm_install.py     install the game from the disc images
tools/lift_health.py    lift statistics and unresolvable jump targets
tools/crt_init_seeds.py seed the catalog with the CRT initializer tables
tools/eh_scan.py        exception-handling structures in Movies.exe, and what a run reached
tools/test_*.py         unit tests
tests/                  regression tests against the real game (M2, M3 boundary)
tests/c/                shim tests in C (no game files)
tests/dxvk/             DXVK Native proof of concept (no game files)
src/host/               the host: boot, import binding, game-specific shims
CMakeLists.txt          host build for 64-bit Linux
docs/                   audit, build steps, milestone reports
```

## Documentation

- [docs/AUDIT.md](docs/AUDIT.md): audit of the reference project and of the
  CD edition, blockers, architecture and roadmap
- [docs/BUILD.md](docs/BUILD.md): reproducible build and run steps
- [docs/M2.md](docs/M2.md): first-boot results
- [docs/M3.md](docs/M3.md): plan to the game window and Direct3D 9 device initialisation; M4 criteria
- [docs/IMPORTS.md](docs/IMPORTS.md): the imports the game calls, their status and what is verified
- [docs/DIRECTSHOW-STARTUP.md](docs/DIRECTSHOW-STARTUP.md): the DirectShow start-up probe and what is implemented for it (not playback or export)
- [docs/D3D9-REQUIREMENTS.md](docs/D3D9-REQUIREMENTS.md): the Direct3D 9 calls the game makes and the device it asks for
- [docs/DXVK-INTEGRATION.md](docs/DXVK-INTEGRATION.md): DXVK Native (pinned v3.1.1) as the Direct3D 9 backend; the proof of concept and the integration decisions
- [docs/ANDROID-FEASIBILITY.md](docs/ANDROID-FEASIBILITY.md): what DXVK requires of an Android GPU, the risks, and how a device is measured
- [docs/EXCEPTIONS.md](docs/EXCEPTIONS.md): C++ EH and SEH in the executable, and what a run reaches (B4)
- [docs/REPOSITORY-AUDIT.md](docs/REPOSITORY-AUDIT.md): correctness audit of the host, its tests and its limits
- [docs/EXTERNAL-REFERENCES.md](docs/EXTERNAL-REFERENCES.md): other projects' findings, checked against this executable

## License

This project's code and documentation are released under the
[MIT License](LICENSE).

The Movies is © 2005 Lionhead Studios / Activision.

## Credits

- [sp00nznet/themovies](https://github.com/sp00nznet/themovies): the Steam 1.2
  recompilation this work builds on (MIT)
- [sp00nznet/pcrecomp](https://github.com/sp00nznet/pcrecomp): the
  recompilation toolchain and the `win32hle` runtime (MIT)
- [Capstone](https://www.capstone-engine.org/), [pefile](https://github.com/erocarrera/pefile),
  [pycdlib](https://github.com/clalancette/pycdlib), [msitools](https://wiki.gnome.org/msitools),
  [cabextract](https://www.cabextract.org.uk/), [SDL](https://www.libsdl.org/)
