# The Movies for Android

A native Android ARM64 port of Lionhead Studios' **The Movies** (2005).
The project statically recompiles the supported Windows executable to C
and replaces platform-specific Windows and DirectX services with native
host implementations.

This is not a remake. It aims to run the original game's code and use the
original game data. You need your own copy of the game.

> **Status (27 November 2026):** A full ARM64 APK containing the user's Russian CD 1.1 game data has been built, installed and launched on a Samsung Galaxy S24 Ultra. The game creates its D3D9 device through Android DXVK Native and continues presenting animated intro frames, but currently remains on the Lionhead splash instead of reaching the main menu. Touch-to-mouse integration and display/lifecycle behaviour are under active validation. A successful APK build or D3D9 diagnostic is not evidence that the game is fully playable.

## How it works

1. Install the game from the original CD images using the repository tools.
2. Analyse and statically recompile the supported executable with
   [pcrecomp](https://github.com/sp00nznet/pcrecomp).
3. Run the lifted code in the host compatibility layer.
4. Replace the game's platform APIs and graphics backend with native
   implementations.

The current supported executable is the Russian CD edition, version 1.1.
Other executable builds require a separate analysis and lift.

## Project status

The Linux x86-64 host reaches the game's own startup, Direct3D 9 device
initialization, main menu and 3D studio lot. On Android, the full game APK
has been installed and launched on a Galaxy S24 Ultra; D3D9 rendering and
repeated frame presentation are observed, but the current on-device run
stalls at the animated intro splash. The Android port is therefore in
runtime-debugging, not feature-complete, status.

| Area | Status |
|---|---|
| CD data extraction and executable lift | Completed for the supported CD 1.1 build |
| Linux host and game rendering | Main menu and studio lot reached |
| Android application shell | Implemented |
| Android SDL/Vulkan/D3D9 diagnostic | Passed on Galaxy S24 Ultra |
| Complete Android ARM64 APK | Built, installed and launched on Galaxy S24 Ultra |
| Android D3D9 game rendering | Device creation and animated intro frames observed |
| Android main menu / gameplay | Not reached; splash-screen stall under investigation |
| Android touch input | Native SDL finger-to-Win32 mouse bridge under validation |

See [Android feasibility](docs/ANDROID-FEASIBILITY.md) for measured device
results and remaining work, [Android port architecture](docs/ANDROID-PORT-AUDIT.md)
for implementation details, and [Android diagnostics](docs/ANDROID-DIAGNOSTICS.md)
for exporting app and native logs without ADB.

## Requirements

For the Linux development build:

- Linux x86-64
- Python 3.10+ and the packages listed in the build documentation
- CMake, Ninja, Clang or GCC, SDL2 and SDL2_ttf
- The pcrecomp toolchain
- The original The Movies CD images

See [docs/BUILD.md](docs/BUILD.md) for installation and build instructions.

## Getting started

Install game data from the original discs:

```bash
python3 tools/tm_install.py --iso ~/iso --out ~/tm/game
```

Then follow [docs/BUILD.md](docs/BUILD.md) to build and run the Linux host.

## Project layout

```
tools/                  installation, analysis and validation tools
tests/                  regression and runtime-boundary tests
src/host/               host runtime and platform compatibility layer
android-probe/          standalone Android Vulkan capability probe
android-d3d9-probe/     standalone Android D3D9 diagnostic
android-game/            Android application and full-game native target
docs/                   build instructions, architecture, implementation notes and test results
```

## Documentation

- [Build instructions](docs/BUILD.md)
- [Technical architecture and executable analysis](docs/AUDIT.md)
- [Host implementation and test coverage](docs/HOST-IMPLEMENTATION.md)
- [Android feasibility and measured results](docs/ANDROID-FEASIBILITY.md)
- [Android port architecture and validation](docs/ANDROID-PORT-AUDIT.md)
- [Android application build](android-game/README.md)
- [Android diagnostics and log export](docs/ANDROID-DIAGNOSTICS.md)
- [Android Vulkan diagnostic](android-probe/README.md)
- [Android D3D9 diagnostic](android-d3d9-probe/README.md)

## Legal

This is an unofficial project and is not affiliated with or endorsed by
Lionhead Studios, Activision or Microsoft. The Movies and related marks
belong to their respective owners.

The public repository does not contain the game's executable, game data,
disc images, assets or generated code derived from the executable. Users
must supply their own game copy. Third-party components and their licences
are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## License

Project code and documentation are released under the
[MIT License](LICENSE).