# Technical architecture and executable analysis

## 1. Target executable

The supported input is the Russian CD edition of The Movies, version 1.1
(Movies.exe, 11,477,288 bytes). It is a 32-bit i386 PE executable built
with MSVC 7.10. The executable is not packed.

| Property | Value |
|---|---|
| PE image base | 0x00400000 |
| Entry point | 0x00A31571 |
| Code range | 0x00401000–0x00C72000 |
| Import table | 329 functions |
| RTTI | 1,879 classes, 2,397 vtables, 7,312 virtual methods |
| Data language | RU-RU |
| Expansion | Stunts & Effects is not included |

The Steam 1.2 / Stunts & Effects executable used by the reference project
is a different binary. Its lifted code and virtual addresses cannot be
reused for this CD executable.

The CD executable contains a .Silvana section with a rebuilt import table
and an import of helper.dll!ConvertSymbols. The helper converts the
localiser's CP1251 Cyrillic range to Unicode. It is not a DRM module.

## 2. Game-data installation

tools/tm_install.py reads the MSI and cabinet files on the three CD images.
It installs the MSI File table without executing InstallShield custom
actions.

Measured installation result:

- 705 of 705 files extracted;
- 427 files read from cabinets and 278 loose files copied;
- 2.08 GB installed;
- 108 MSI size mismatches caused by post-MSI localisation replacements;
- no missing files and no cabinet checksum failures.

The installed game data and disc images are not stored in this repository.

## 3. Function catalog and static recompilation

The catalog and lifted C are generated from the supported CD executable.

| Stage | Result |
|---|---:|
| Disassembly catalog | 85,894 functions |
| Instructions decoded | approximately 6.0 million |
| .text byte coverage | 85.6% |
| C/C++ initializer seeds | 702 |
| Lifted functions | 85,910 |
| Lift exceptions | 0 |
| Not-lifted stubs | 0 |
| Generated C translation units | 215 |
| Generated C source | approximately 10 million lines |
| Extent walks without recognized terminator | 559 |
| Unresolved indirect-tail targets | 7 |

The seven unresolved indirect-tail targets and the 559 incomplete extent
walks remain analysis risks. A successful lift is not proof of complete
control-flow recovery or semantic equivalence.

Generated C is created from the user's executable and remains private.
It is supplied to the private Android build workflow and is not committed
to this public repository.

## 4. Runtime model

The project uses static recompilation, not x86 instruction emulation.

- Lifted code is compiled as native host C.
- Guest registers and guest virtual addresses are represented by the
  pcrecomp recompilation runtime.
- The 32-bit guest address space is mapped inside a 64-bit host process
  using pcrecomp's based-memory model.
- Win32 imports are implemented by pcrecomp/win32hle and project-specific
  shims in src/host/.
- Guest-visible pointers must refer to guest memory; host pointers must
  not escape through Win32 or COM interfaces.
- SDL2 supplies host window and event integration.

Linux and Android use separate application targets. Linux builds the
tm_host executable. Android builds the SDL shared-library entry point
main for arm64-v8a.

## 5. Platform graphics backends

The guest uses Direct3D 9. The host bridge converts guest COM interfaces,
structures and pointers into host-side calls.

| Platform | D3D9 backend | Verified result |
|---|---|---|
| Linux x86-64 | DXVK Native 3.1.1 | The game's device initializes; intro, main menu and studio-lot rendering have been reached |
| Android ARM64 | Android-adapted DXVK Native 1.9.2 fork | Standalone D3D9 diagnostic passes on Galaxy S24 Ultra; full game runtime not yet verified |

The Android fork is pinned separately from the Linux dependency. The
stock Adreno 750 driver does not expose all extensions required by the
Linux DXVK 3.1.1 build. The Android D3D9 result applies to the tested fork
and diagnostic workload only.

See D3D9-REQUIREMENTS.md, DXVK-INTEGRATION.md and
ANDROID-FEASIBILITY.md.

## 6. Runtime subsystem coverage

| Subsystem | Implementation / status |
|---|---|
| Win32 imports | pcrecomp/win32hle plus project-specific host shims; see IMPORTS.md |
| Guest memory and PE loader | pcrecomp based-memory runtime and project host integration |
| Direct3D 9 | Guest-to-host bridge; DXVK Native backend |
| DirectInput 8 | SDL-backed host implementation; Android touch mapping not validated |
| DirectShow startup probe | Startup COM objects implemented; media playback is not |
| Video playback | Not implemented |
| Audio | Host interfaces and dependencies incomplete; device playback not verified |
| C++ exceptions / SEH | Guest frame records are present in lifted code; guest exception dispatch is not implemented |
| Network services | Not required for offline operation; unsupported paths must fail explicitly |
| Save/load | Linux game paths are available; Android save/load lifecycle not verified |

## 7. Execution results

The following Linux execution results are verified:

- CRT initialization reaches the executable's WinMain.
- The startup DirectShow probe completes.
- The game's Direct3D 9 device is created through DXVK Native.
- The intro logo, main menu and a new 3D studio lot have been reached.

Android validation is separate. The D3D9 diagnostic passed on the
Galaxy S24 Ultra, but the complete game has not yet reached its CRT,
WinMain, D3D9 initialization or first frame on Android.

## 8. Technical references

- BUILD.md — build and run
- M2.md — startup result
- M3.md — graphics initialization
- IMPORTS.md — Win32 imports
- D3D9-REQUIREMENTS.md — D3D9 requirements
- DIRECTSHOW-STARTUP.md — DirectShow startup contract
- EXCEPTIONS.md — guest exception handling
- ANDROID-PORT-AUDIT.md — Android ARM64 architecture
