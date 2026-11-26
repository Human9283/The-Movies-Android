# The Movies for Android

Native ARM64 port of *The Movies* (2005), based on static recompilation of the supported Windows executable. This is not an emulator or a remake.

**Supported input:** Russian CD edition, version 1.1. Users must supply their own game discs and installed data. No executable, game assets or generated game code are distributed here.

## Verified status

| Area | Result |
|---|---|
| Game-data extraction | 705/705 files extracted on Linux |
| Executable analysis and lift | 85,910 lifted entries; 0 lift exceptions; 7 unresolved indirect-tail targets remain |
| Linux x86-64 runtime | Reaches WinMain, Direct3D 9 initialization, intro, main menu and 3D studio lot |
| Linux graphics | DXVK Native 3.1.1; tested with software Vulkan and host GPU configurations |
| Android ARM64 APK | Built, installed and launched on Samsung Galaxy S24 Ultra |
| Android game rendering | D3D9 device creation and repeated animated intro frames observed |
| Android main menu / gameplay | Not reached; runtime currently remains on the Lionhead splash |
| Android touch input | SDL finger-to-Win32 mouse bridge added; on-device effectiveness not yet confirmed |
| Video, audio, input, exceptions | Incomplete or not fully verified; see subsystem documents |

The Android game currently reaches its animated intro rendering loop but does not reach the main menu. A successful lift, native build, APK build or standalone graphics diagnostic is not evidence that the complete game is playable.

## Build and run

Requirements, dependency revisions, commands and expected test results are in [docs/BUILD.md](docs/BUILD.md).

Install from the original CD images:

```bash
python3 tools/tm_install.py --iso ~/iso --out ~/tm/game
```

Build the host following the build guide. Run the game:

```bash
cd ~/tm/game
~/tm/build/tm_host . --d3d9 dxvk
```

Run tests:

```bash
python3 -m unittest tools/test_tm_install.py
ctest --test-dir build --output-on-failure
python3 tests/m2_winmain.py --host build/tm_host --game /path/to/game
python3 tests/m3_boundary.py --host build/tm_host --game /path/to/game
```

## Technical documentation

- [AUDIT.md](docs/AUDIT.md) — executable, static recompilation, runtime architecture and verified execution results
- [BUILD.md](docs/BUILD.md) — reproducible build and test procedure
- [HOST-IMPLEMENTATION.md](docs/HOST-IMPLEMENTATION.md) — host subsystems, implementation limits and test coverage
- [M2.md](docs/M2.md) — CRT initialization and WinMain evidence
- [M3.md](docs/M3.md) — Direct3D 9 initialization and subsequent Linux rendering
- [IMPORTS.md](docs/IMPORTS.md) — Win32 import inventory and implementation status
- [D3D9-REQUIREMENTS.md](docs/D3D9-REQUIREMENTS.md) — game D3D9 calls and backend requirements
- [DXVK-INTEGRATION.md](docs/DXVK-INTEGRATION.md) — DXVK Native integration and measured limits
- [DIRECTSHOW-STARTUP.md](docs/DIRECTSHOW-STARTUP.md) — DirectShow startup contract; not media playback
- [EXCEPTIONS.md](docs/EXCEPTIONS.md) — guest C++ exception and SEH analysis
- [ANDROID-FEASIBILITY.md](docs/ANDROID-FEASIBILITY.md) — Android requirements, device evidence and remaining work
- [ANDROID-PORT-AUDIT.md](docs/ANDROID-PORT-AUDIT.md) — Android application architecture and runtime validation
- [ANDROID-DIAGNOSTICS.md](docs/ANDROID-DIAGNOSTICS.md) — on-device log export without ADB
- [Android application build](android-game/README.md) — APK structure and private CI build inputs
- [ANDROID-VULKAN-PROBE.md](docs/ANDROID-VULKAN-PROBE.md) — standalone Vulkan diagnostic workflow
- [EXTERNAL-REFERENCES.md](docs/EXTERNAL-REFERENCES.md) — upstream source references and pinned revisions

## Repository contents

`src/host/` contains the platform host and compatibility shims. `tools/` contains installer and analysis tools. `tests/` contains regression tests and diagnostic programs. `patches/` contains project patches. `docs/traces/` contains measured runtime traces.

## Legal and licensing

This is an unofficial project and is not affiliated with Lionhead Studios, Activision or Microsoft. The Movies and related marks belong to their respective owners.

The repository does not distribute the game executable, game data, disc images, assets or generated code derived from the executable. Project code and documentation are MIT-licensed; third-party components retain their respective licences. See [LICENSE](LICENSE) and [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
