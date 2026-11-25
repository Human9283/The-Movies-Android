# Technical audit: porting The Movies (2005) to Android ARM64

Stage: **P0 audit**, updated with the M1 results. Date: 2026-11-24.

References: [sp00nznet/themovies](https://github.com/sp00nznet/themovies) at
`8e278f6` (2026-09-30) and its toolchain
[sp00nznet/pcrecomp](https://github.com/sp00nznet/pcrecomp) at `7c07a0b`
(2026-11-22), both MIT.

Every claim is tagged:
- **[meas.]** measured here, on the user's copy or on the reference code;
- **[doc.]** taken from the reference project's documentation, not re-checked;
- **[hyp.]** a hypothesis still to be tested.

---

## 1. The reference project

It is a **static recompilation**, not an emulator: every x86 function of
`MoviesSE.exe` is translated to a C function (`lift32`), and the whole is
compiled back into one process.

| Stage | Tool | Result on Steam 1.2 [doc.] |
|---|---|---|
| Unpack | `drm/emu_unpack.py` (Unicorn): PECompact 2.x + Steam2 check | `MoviesSE.unpacked.exe`, OEP `0x00AD2451` |
| RTTI | `cpp/rtti.py` | 1,992 classes, 9,227 virtual methods |
| Function catalog | `disasm/disasm32.py`, seeded from RTTI | 82,109 functions, 84.6% of `.text` |
| Lift | `run_lift.py` → `lift/generate.py` + `lift32.Lifter(precise_carry=True)` | 83,468 functions, 9.8M lines of C, 0 errors |
| Host | `src/runtime/host.c` on `runtime/native32` | 32-bit exe, MSVC x86 |
| Check | `tools/conformance.py` | 7/7 boot milestones, main menu, a new studio by scripted input |

### 1.1 The reference execution model [meas., from source]

- Guest registers are C globals (`g_eax`, `g_esp`, ...). Guest memory is read
  through `MEM32(addr)` = `*(uint32_t*)ADDR(addr)`
  (`runtime/recomp32/recomp_types.h`). With `RECOMP_FLAT_MEMORY`,
  `ADDR(va) == va`: a guest address **is** a host address.
- The host is 32-bit and maps the guest image 1:1 at `0x00400000`.
- Win32 imports are the **real** ones. `native_bridge` copies the arguments
  to the host stack and calls Windows: MSVC x86 inline assembly
  (`runtime/native32/native32.h`).
- Callbacks (WndProc, thread starts): the guest's `.text` is mapped
  non-executable, and a vectored exception handler redirects the fault to
  the lifted function.
- D3D9, DirectInput 8, DirectShow, DirectSound/OpenAL are **the real Windows
  DLLs**, loaded with `LoadLibrary`/`GetProcAddress` and called through their
  vtables.
- Guest SEH and C++ exceptions are handled by Windows itself: the SEH frames
  live on the guest stack, which is also the host stack.
- SIMD (~13,700 SSE/SSE2/3DNow! sites) is not lifted. The host hides SSE via
  `CPUID`/`IsProcessorFeaturePresent`, so the game takes its x87 paths.

**Conclusion:** the reference lift's output is portable (it is C); the
reference host is not. It rests on three Windows x86 properties: 32-bit host
pointers, real system DLLs, and VEH plus inline assembly.

### 1.2 What pcrecomp already has off Windows [doc. + source]

`runtime/win32hle/` is an **implemented Win32** on libc, POSIX and SDL2. An
import is a C function inside the lifted model; a native-to-guest call is an
explicit `hle_call_guest()`. No inline assembly, no OS exception handler. It
has a **based-memory** mode for 64-bit hosts: the guest's 4 GB is one
mapping, `ADDR(va) = va + base`, and everything the guest gets a pointer to
(heaps, host COM objects, per-call structures) is allocated inside it.

Per the pcrecomp README, Tiberian Sun, Red Alert 2 and Civilization III run
this way on Linux. Measured here: all 15 win32hle selftests pass on Linux
x86-64 in based mode (`HLE_BASED=1 build_selftests.sh`). [meas.]

What win32hle **lacks** and The Movies needs:

| Subsystem | In win32hle | The Movies needs |
|---|---|---|
| Direct3D | DirectX 6 (DDraw + D3D3), software rasteriser, Metal on macOS | **Direct3D 9** (`d3d9.dll` via `LoadLibrary`); D3DX is linked into the game |
| Video | Bink via FFmpeg | **DirectShow**: `CLSID_FilterGraph`, `CLSID_WMAsfReader`, the game's own `CTextureRenderer@MV` filter [doc.] |
| Input | DirectInput 3/5 | **DirectInput 8** (`DirectInput8Create`) |
| Audio | DirectSound 1 on SDL2 | DirectSound 8 **or** OpenAL (`OpenAL32.dll`/`wrap_oal.dll`) |
| Codecs | none | `msacm32` (7 `acmStream*` functions); Ogg Vorbis is linked into the game |
| Exceptions | `RaiseException` ends the run (`kernel32_crt.c`) | MSVC 7.1 C++ EH (`_CxxThrowException` → `RaiseException`), CRT SEH |
| Film export | none | `WMVCore.dll` (writing .wmv) |
| Network | Winsock 1.1 | `ws2_32` (29 functions), only for the defunct Movies website |

pcrecomp also has a guest C++ EH runtime (`runtime/native32/eh32.c`: setjmp
landing pads per lifted frame, `__CxxFrameHandler` dispatch). It is built for
native32 (`windows.h`, `__declspec(thread)`), but its design does not depend
on Windows. [meas., from source]

---

## 2. The user's copy: Russian CD edition ("Фабрика грёз")

### 2.1 Discs [meas.]

| Image | Label | Size | Contents |
|---|---|---:|---|
| MOVIES_1.iso | MOVIES_1 | 641,177,600 | InstallShield 9 MSI, `Movies.exe`, `StarMaker.exe`, `helper.dll`, videos, some paks, `SECDRV.SYS` + `DrvMgt.dll` (SafeDisc driver) |
| MOVIES_2.iso | MOVIES_2 | 691,412,992 | `a_soun~1.cab`, `a_1/a_2_at~1.cab`, `meshscene000{0,1}.pak`, `PATCH1.pak` |
| MOVIES_3.iso | MOVIES_3 | 680,204,288 | `a_LOCS.cab`, `TEX_ALL000{0,1}.pak`, `TEX_COS_0000.pak` |

`version.inf`: **ExtVersion=1.1**, IntVersion=511.0, QA=4096, Language=134.
Data language `RU-RU`. No Stunts & Effects expansion.

### 2.2 The executable [meas.]

| | CD 1.1 | Steam (reference) [doc.] |
|---|---|---|
| File | `Movies.exe` | `MoviesSE.exe` (with S&E), `Movies.exe` |
| Version | 1.1 (FileVersion) | 1.2 |
| Size | 11,477,288 | 3,120,128 (packed) |
| SHA-1 | `(hash removed)` | `(hash removed)` (SE), `(hash removed)` (base) |
| Packing | **none**: `.text` entropy 6.50, separate `.rdata`/`.data` | PECompact 2.x + Steam2, one RWX section |
| Entry point | `0x00A31571`, stock MSVC 7.1 CRT (`push 60h / push scope / call __SEH_prolog`) | `0x00AD2451` after unpacking (SE) |
| Linker | 7.10 | 7.10 |
| Imports | 329 functions from 12 DLLs | 330 from 11 |
| PE timestamp | zeroed (1970) | 2006-04-27 (SE) |
| RTTI | 1,879 classes, 2,397 vtables, 7,312 virtual methods | 1,992 classes, 9,227 virtual methods |

**The executable was modified after it was built:**
- A `.Silvana` section (0x1000 bytes) holds a **rebuilt import table** with
  one new import, `helper.dll!ConvertSymbols`.
- `helper.dll` (40,960 bytes, 2006-02-26) is **not protection**. Disassembled,
  `ConvertSymbols(dst, src, n)` copies `n` UTF-16 units and adds 0x350 to
  0xC0–0xFF, turning CP1251 А–я into U+0410–U+044F. It is called once, at
  `0x00403632` inside a string-copy routine, followed by three `nop`s: the
  localiser's Cyrillic fix.
- No SafeDisc sections (`stxt*`) in the executable, although `SECDRV.SYS` is
  on the disc.
- The MSI's File table expects `Movies.exe` at 11,513,856 bytes, version
  1.0.0.0; the disc has 11,477,288 bytes, version 1.1. The same holds for
  `StarMaker.exe`, `stringdatabase.lhts` and 105 tutorial videos: the files
  were replaced after the MSI was built (InstallShield does not check sizes).

**Imports compared with Steam SE**, ignoring the `ws2_32`/`oleaut32` imports
the reference lists by ordinal:
- CD only: `helper.dll!ConvertSymbols`, `user32!RegisterClipboardFormatA`;
- Steam SE only: `kernel32!SetHandleCount`, `user32!RegisterWindowMessageA`,
  `ToAscii`, `ToUnicode`.

The same run-time DLLs are named in the strings: `d3d9.dll`, `dinput8.dll`,
`dsound.dll`, `OpenAL32.dll`, `WMVCore.dll`, `wininet.dll`, plus
`Direct3DCreate9`, `DirectInput8Create`, `alcOpenDevice`.

### 2.3 Compatibility with the reference

1. **The reference's lifted code does not apply to the CD build.** It is a
   different binary: version 1.1, not 1.2; different addresses (OEP
   `0x00A31571` vs `0x00AD2451`); no S&E; the localiser's patch. Lifted C is
   tied to virtual addresses. [meas.]
2. **The reference pipeline applies.** Same compiler (MSVC 7.1), same CRT,
   RTTI on, nearly the same imports. No unpack step: the executable is not
   packed. [meas.] With separate sections, `disasm32` covered 85.6% of
   `.text` versus 84.6% on the merged Steam image. [meas.]
3. **`helper.dll` needs no lifting**: one C shim (`src/host/tm_host.c`). [meas.]
4. What the reference learned about the engine (video through DirectShow,
   `precise_carry` for 64-bit `REFERENCE_TIME` arithmetic, hidden SIMD, the
   DirectInput cursor accumulator) very likely holds for 1.1. [hyp.] The
   addresses in the reference host (`0x00554490`, `0x0104CCF0`, ...) do not;
   they must be found again. [meas.: they belong to SE]

### 2.4 Install without Windows (M0) [meas.]

`tools/tm_install.py` reads the MSI on disc 1 and places **all 705** rows of
its File table: 427 from cabinets on the three discs, 278 stored loose. No
InstallShield custom action runs.

```
[msi] 705 files, 10 media, 69 components, 257 directories
[cab]  Audio_~1.cab MOVIES_1 6 | tutori~1.cab MOVIES_1 106 | a_1_at~1.cab MOVIES_2 38
[cab]  a_2_at~1.cab MOVIES_2 45 | a_soun~1.cab MOVIES_2 1 | a_LOCS.cab MOVIES_3 231
[loose] 278 files
[done] 705 of 705 files, 2.08 GB, 108 problems
```

All 108 "problems" are size differences from the File table caused by the
localiser's replacements (2.2). No file is missing and no cabinet fails its
checksum. `Data/pak`: 8 archives, 891 MB. `Data/Audio`: 888 MB.

### 2.5 Catalog and lift (M1) [meas.]

| Step | Result |
|---|---|
| `pe_analyze.py` | code `0x00401000-0x00C72000`, IAT at `0x00C72000`, 329 imports |
| `rtti.py` | 1,879 classes, 2,397 vtables, 7,312 virtual methods |
| `disasm32.py` | 85,894 functions, 6.0M instructions, 85.6% byte coverage, about 7 min |
| `crt_init_seeds.py` | 697 C++ and 5 C initializers added to the seeds |
| `run_lift.py --all` | 85,910 functions, 0 errors, 0 not-lifted stubs, 559 with no terminator, 10.0M lines in 215 files, about 3 min |
| `tools/lift_health.py` | 7 unresolvable ITAIL labels (reference: 10) |

---

## 3. The x86 reference model vs Android ARM64

| Aspect | Reference (Windows x86) | Android ARM64 | Consequence |
|---|---|---|---|
| Host width | 32-bit; guest pointer = host pointer | 64-bit only (arm64-v8a); current flagship cores have no AArch32 [hyp.] | based memory: the guest's 4 GB in one mapping, `ADDR(va)=va+base` |
| Image placement | 1:1 at `0x00400000` | wherever `mmap` puts it, ASLR | no host pointer may reach the guest; everything through `hle_alloc` |
| System DLLs | real Win32/DirectX | none | everything the guest calls is implemented (HLE) |
| Import calls | `native_bridge`, MSVC asm | C shim in the lifted model (`A32`/`RET`) | win32hle, no asm |
| Callbacks | VEH + non-executable `.text` | no VEH | explicit `hle_call_guest()` |
| Guest COM → host | real DLL vtables | none | guest vtables whose slots are shim VAs; every D3D9/DInput8/DShow method is a shim |
| SEH / C++ EH | the Windows kernel | none | walk the guest `FS:[0]` chain ourselves; whether this works in lifted code is **unknown** (B4) |
| Byte order | LE | LE | unchanged |
| x87 | lifted FPU on `double` | same | 80 → 64-bit precision, as in the reference |
| SSE/3DNow! | hidden | hidden | unchanged; NEON is a later optimisation |
| Threads | one machine lock | same (win32hle) | pthreads; one guest thread runs lifted code at a time |
| Graphics | D3D9 → GPU driver | Vulkan 1.3 or GLES 3.2 | a D3D9 translator (5.3) |
| Audio | Windows DirectSound/OpenAL | AAudio/OpenSL ES | OpenAL Soft or SDL2 audio |
| Video | DirectShow + OS WMV codecs | none | ASF demuxer + WMV/WMA decoder (FFmpeg) behind a DirectShow graph |
| Input | mouse/keyboard via DInput8 | touch screen | touch → DInput8 mouse, on-screen keyboard |
| Files | NTFS, case-insensitive, `\` | ext4/f2fs, case-sensitive, scoped storage | `hle_path.c`: case-insensitive lookup; data in `Android/data/<pkg>/files` |
| Registry | real | none | in-memory registry seeded from a file (win32hle `advapi32.c`) |
| Build | MSVC, 15 min for 9.8M lines | NDK clang, arm64 | measured on Linux x86-64: 1,299 s with clang -O1 on 4 cores, 687 MB binary (252 MB of code) |

---

## 4. Blockers for Android ARM64

Ordered by how early each one stops the path.

| # | Blocker | Severity | Status |
|---|---|---|---|
| B1 | **No lifted code for CD 1.1.** | required | **resolved in M1**: 85,910 functions, 0 errors |
| B2 | **The reference host is Win32 x86 only** (native32: MSVC asm, VEH, real DLLs). | required | replaced by win32hle; host in `src/host/` |
| B3 | **No Direct3D 9** in any portable pcrecomp layer: D3D9, shaders, fixed function → Vulkan/GLES. | critical, the largest | options in 5.3 |
| B4 | **Guest C++ exceptions and SEH** are not handled by win32hle. Unknown whether the game throws on normal paths. | possibly critical | measure in M3 |
| B5 | **DirectShow + WMV**: intro, menu backdrop (`frontend_loop.wmv`), tutorials, credits. The reference showed a black menu without video. | high | an HLE graph for one shape (file → ASF reader → the game's filter) |
| B6 | **Audio**: DirectSound 8 or OpenAL, plus `msacm32` (codecs unknown). | high | prefer OpenAL (a C API, no COM); confirm the game picks it |
| B7 | **DirectInput 8** instead of 3/5. | medium | extend `dinput.c` |
| B8 | **64-bit host**: everything the guest holds a pointer to must live inside its 4 GB. | cross-cutting | a design rule for every shim |
| B9 | **Performance** of lifted code on ARM64: registers in globals or locals, software x87, the machine lock. | unknown | measure |
| B10 | **Build**: ~10M lines of C through the NDK; time, memory and `.so` size on arm64 unknown. | medium | measure |
| B11 | **Android file system**: case sensitivity, scoped storage, 2.1 GB of data. | medium | `adb push` to the app directory first; in-app import later |
| B12 | **Network and WMV export** (`wininet`, `WMVCore`). | low | offline: graceful failures; film export last |
| B13 | **60 bound imports without an implementation** (docs/M2.md). | grows with each run | implement as the run reaches them |

Not blocking: unpacking and DRM (the CD executable is not packed), byte
order (LE on both sides), StarMaker (a separate program, optional).

---

## 5. Proposed architecture

```
 ┌──────────────── APK (arm64-v8a) ────────────────────────────────────────┐
 │  Java/Kotlin: Activity, data import, settings, on-screen keyboard       │
 │  SDL2 (window, input, audio device)                                     │
 │ ┌──────────────── libthemovies.so ────────────────────────────────────┐ │
 │ │ gen/  - lifted Movies.exe 1.1                                       │ │
 │ │ recomp32 runtime: registers, based MEM32, x87, flags, dispatch      │ │
 │ │ win32hle (pcrecomp, MIT): kernel32, user32, gdi32, advapi32, winmm, │ │
 │ │         ole32, paths, heaps, threads, registry                      │ │
 │ │ tm-hle (this repository):                                           │ │
 │ │   d3d9  → [D3D9 translator] → Vulkan / GLES                        │ │
 │ │   dinput8 → SDL touch/mouse/keyboard                               │ │
 │ │   openal (or dsound8) → OpenAL Soft / SDL audio                    │ │
 │ │   dshow: FilterGraph + WMAsfReader → FFmpeg (WMV/WMA) → game filter│ │
 │ │   msacm32, helper.dll, shell folders, ws2_32 (offline)             │ │
 │ │   seh32: guest SEH/C++ EH (if B4 is confirmed)                     │ │
 │ └─────────────────────────────────────────────────────────────────────┘ │
 └─────────────────────────────────────────────────────────────────────────┘
   game data: Android/data/<pkg>/files/game/  (tm_install.py → adb push)
```

### 5.1 Decisions

- **D1. Static recompilation, not x86 emulation** (Box86/FEX + Wine).
  Emulation on Android is real (Winlator), but it is a launcher for an exe,
  not a native port. Recompilation gives native arm64 code of the game
  itself and keeps all of its logic, because it translates the same
  instructions.
- **D2. win32hle, not native32.** It is the only pcrecomp layer that runs
  without Windows and without assembly, it is MIT, and it already hosts
  2000–2003 games on 64-bit Linux.
- **D3. Licences.** MIT, BSD, zlib, and LGPL linked dynamically (FFmpeg,
  OpenAL Soft) are acceptable. GPL code (Wine, DOSBox) is not copied in.
- **D4. Linux first, then Android.** Each subsystem is brought up on Linux
  x86-64 and arm64 in based mode (the same 64-bit pointers), with a
  debugger, sanitizers and frame recordings, and moves to Android once it
  works.

### 5.2 Guest memory on a 64-bit host

One 4 GB `PROT_NONE` reservation, `Movies.exe` at offset `0x00400000`. A
fixed `RECOMP_MEM_BASE` (a constant add) is faster than `g_mem_base` (a
global the compiler reloads), but needs a fixed address in Android's 39/48-bit
address space. Decided by measurement: try `MAP_FIXED_NOREPLACE` at a
constant, fall back to the variable. The current Linux build uses the
variable.

### 5.3 D3D9 → Vulkan/GLES: options

| Option | For | Against |
|---|---|---|
| **A. DXVK-native (d3d9) + a thin guest→host layer** | mature D3D9 on Vulkan, zlib licence, builds without Wine | every D3D9 interface method needs a shim that converts 32-bit structures and pointers; NDK/Adreno build unproven |
| **B. Own D3D9 → GLES 3.2/Vulkan**, only what The Movies calls | full control, nothing extra | large; SM1–2 shader translation written from scratch |
| **C. Software rasteriser** (like `d3d_raster.c`) | a first picture quickly | far too slow for the 3D studio lot |

**Recommendation:** A as the main path, B as the fallback. Decided after M3,
from a log of every D3D9 method, format and shader the game actually uses.

---

## 6. Plan and success criteria

Each milestone is checked on its own, with a measurable criterion.

| Milestone | What | Done when |
|---|---|---|
| **M0** ✅ | Install from the ISOs without Windows (`tools/tm_install.py`) | 705/705 files, 0 missing, 0 CAB CRC errors, sha256 manifest; MSI logic unit tests pass |
| **M1** ✅ | Catalog and lift of CD `Movies.exe` | 0 lift errors; ≤ 20 unresolvable ITAILs (7); coverage recorded (85.6%) |
| **M2** (x86-64 ✅, docs/M2.md) | win32hle host in based mode: CD CRT on **Linux x86-64**, then **arm64**, then as a console binary on an Android device via `adb shell` | on each: the CRT runs without faults into `WinMain`, shown by the dispatch trace; functions and imports used, and time to `WinMain`, recorded |
| **M3** (docs/M3.md) | On to the game's own `CreateDevice`, through a recording D3D9 layer | `CreateDevice` succeeds; resources and the rendering calls the game reaches are traced; a CreateDevice test and trace; `docs/D3D9-REQUIREMENTS.md`; B4 measured |
| **M4** | The rendering path through the chosen option, no video | the first frame from the game's own rendering code captured on Linux, kept with its D3D9 trace; then on Android |
| **M5** | DirectShow/WMV, OpenAL, DInput8 | intro with video and sound; main menu with its backdrop; a tap registers |
| **M6** | The game: a new studio, a year on the lot, save and load | the reference script (Game → Quick Start → studio) passes by touch; a save survives a restart |
| **M7** | APK: data import, fullscreen, Activity lifecycle | install the APK and the data, run without a PC, offline |

---

## 7. Open questions

1. Does the game call `RaiseException`/`_CxxThrowException` on normal paths
   (B4)? M3 answers it.
2. Which audio path does the game choose when both `dsound.dll` and
   `OpenAL32.dll` exist?
3. Which ACM formats are needed (are the `.lug` banks ADPCM)?
4. Which shader versions and D3D9 caps does the game require?
5. Is lifted code with software x87 fast enough on ARM64 (B9)?
