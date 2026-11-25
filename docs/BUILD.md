# Build and run (Linux, 64-bit host)

Tested on Ubuntu 24.04 x86-64 with clang 18, CMake 3.28, Ninja and Python
3.13. Paths below use `~/tm` as a working directory; any directory works.

## 1. Prerequisites

```bash
sudo apt install msitools cabextract libsdl2-dev libsdl2-ttf-dev clang cmake ninja-build
```

```bash
pip install pefile capstone unicorn pycdlib
```

```bash
git clone https://github.com/Human9283/The-Movies-Android ~/tm/the-movies-android
```

```bash
git clone https://github.com/sp00nznet/pcrecomp ~/tm/pcrecomp
```

```bash
git clone https://github.com/sp00nznet/themovies ~/tm/themovies
```

Tested revisions: pcrecomp `7c07a0b`, themovies `8e278f6`.

## 2. Install the game data

Put `MOVIES_1.iso`, `MOVIES_2.iso` and `MOVIES_3.iso` in one directory:

```bash
python3 ~/tm/the-movies-android/tools/tm_install.py --iso ~/iso --out ~/tm/game
```

Expected: `[done] 705 of 705 files, 2.08 GB, 108 problems` (the 108 are the
localiser's replaced files, docs/AUDIT.md 2.4).

## 3. Function catalog (M1)

```bash
mkdir -p ~/tm/work && cd ~/tm/work
```

```bash
python3 ~/tm/pcrecomp/tools/pe/pe_analyze.py ~/tm/game/Movies.exe --json pe_analysis.json
```

```bash
python3 ~/tm/pcrecomp/tools/cpp/rtti.py ~/tm/game/Movies.exe -o rtti.json --seeds rtti_seeds.json
```

Expected: `classes : 1,879`, `virtual methods : 7,312`.

Add the CRT's initializer tables (the game's global constructors) to the
seeds, so none is missed by the catalog:

```bash
python3 ~/tm/the-movies-android/tools/crt_init_seeds.py ~/tm/game/Movies.exe --merge rtti_seeds.json -o seeds.json
```

Expected: `table 0x00D9E000-0x00D9EAE8: 698 slots, 697 initializers`,
`table 0x00D9EAEC-0x00D9EB04: 6 slots, 5 initializers`.

```bash
python3 ~/tm/pcrecomp/tools/disasm/disasm32.py ~/tm/game/Movies.exe -o functions.json --seed-functions seeds.json
```

Expected: `Byte coverage: 7,580,053 / 8,851,456 (85.6% of code range)`.
About 7 minutes.

## 4. Lift to C

The reference lift driver (`themovies/run_lift.py`) reads `rtti_seeds.json`
from its own `work/` directory, so link it there:

```bash
ln -sfn ~/tm/work ~/tm/themovies/work
```

```bash
cd ~/tm/themovies && PCRECOMP=~/tm/pcrecomp python3 run_lift.py --exe ~/tm/game/Movies.exe --catalog ~/tm/work/functions.json --out ~/tm/work/gen --all
```

Expected: `lifted 85910   not-lifted stubs 0   errors 0   no terminator 559`,
10.0M lines in 215 files, about 3 minutes.

```bash
python3 ~/tm/the-movies-android/tools/lift_health.py ~/tm/work/gen
```

Expected: `unresolved ITAIL : 7`.

## 5. Build the host

```bash
cmake -S ~/tm/the-movies-android -B ~/tm/build -G Ninja -DCMAKE_C_COMPILER=clang -DPCRECOMP=$HOME/tm/pcrecomp -DTM_GEN=$HOME/tm/work/gen
```

```bash
cmake --build ~/tm/build -j 3
```

About 22 minutes on 4 cores; the binary is about 690 MB. `-j 3` keeps memory
use within 16 GB.

Check that the lifted code is linked (85,910 functions):

```bash
nm ~/tm/build/tm_host | grep -c " T sub_"
```

### 5.1 DXVK Native (optional)

The Direct3D 9 backend ([DXVK-INTEGRATION.md](DXVK-INTEGRATION.md)). It needs
meson, glslang and the Vulkan headers:

```bash
sudo apt install glslang-tools libvulkan-dev
```

```bash
pip install meson
```

```bash
git clone --depth 1 --branch v3.1.1 --recurse-submodules https://github.com/doitsujin/dxvk ~/tm/dxvk
```

Pinned: tag `v3.1.1`, commit `b1a1c99ab52b687cf950d62c88bc2fa316b41663`.

```bash
meson setup ~/tm/dxvk-build ~/tm/dxvk --buildtype=release --prefix=$HOME/tm/dxvk-install -Denable_dxgi=false -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Dnative_sdl2=enabled -Dnative_sdl3=disabled -Dnative_glfw=disabled
```

```bash
ninja -C ~/tm/dxvk-build install
```

About 2 minutes on 4 cores. Then add `-DTM_DXVK_PREFIX=$HOME/tm/dxvk-install`
to the `cmake -S` command above. That builds `dxvk_present`, the proof of
concept. If `xvfb-run` is installed (`sudo apt install xvfb`), it also adds the
`dxvk_present` CTest. That test needs a Vulkan 1.3 driver. Without a GPU,
Mesa's lavapipe works (`sudo apt install mesa-vulkan-drivers`). Run it by
hand:

```bash
DXVK_WSI_DRIVER=SDL2 xvfb-run -a -s "-screen 0 1024x768x24" ~/tm/build/dxvk_present 1024 768 24 0
```

The arguments are width, height, `D3DFORMAT` and windowed (0 or 1).

## 6. Run

```bash
cd ~/tm/game && ~/tm/build/tm_host ~/tm/game --stop-at-winmain --watchdog 120 --user ~/tm/user --system ~/tm/system
```

Run it from the game directory, as a Windows shortcut starts the game: the
guest's current directory is the host's, and the start-up path (and so the
function counts below) differs with it (docs/M2.md 2).

| Option | Meaning |
|---|---|
| `<game dir>` | the install from step 2; the guest sees it as `C:\` |
| `--stop-at-winmain` | exit 0 as soon as `WinMain` (`0x005303C0`) is entered |
| `--watchdog S` | stop after S seconds (exit 4) |
| `--user DIR` | the guest's `D:\`: My Documents, Application Data, Local Settings\Temp (default `./tm_user`) |
| `--seen-out FILE` | write every function entry address the run executed (`%08X`, one per line) on exit, on a crash and at `--stop-at-winmain`; input to `tools/eh_scan.py` |
| `--d3d9 dxvk` | open the game's window and create its Direct3D 9 device on DXVK Native (only in a build with `TM_DXVK_PREFIX`, 5.1); without it the run is headless and stops at `CreateDevice` |
| `--mouse free` | do not hold the mouse in the window (win32hle holds it while the window has focus). For runs on Xvfb, whose pointer barriers stop win32hle's cursor warps |
| `--system DIR` | the guest's `C:\WINDOWS` (with `SYSTEM32`), the runtime system directory (default `./tm_system`); the game, user and system directories may not be inside one another |

Output lines:

| Line | Meaning |
|---|---|
| `[bind] STUB name` | an import with no implementation; calling it stops the run |
| `[poc] WinMain 0x005303C0 entered` | the CRT reached `WinMain`; the line gives time and function counts |
| `[shell] ...`, `[MessageBoxW] ...` | our shims answering the game |
| `ICALL: unresolved VA` | an indirect call into code that was not lifted: a defect to fix |
| `=== abort / SIGSEGV ...` | crash report: function, last import, registers, guest stack, last 32 function entries |

Clicking through the menus without a desktop: `tools/xvfb_clicks.sh HOST
GAME OUT WAIT X,Y[:PAUSE]...` runs the game on its own Xvfb screen and
clicks the given points of the 1024×768 picture (needs `Xvfb`, `xdotool`
and ImageMagick's `import`; the script says how it aims).
`TM_DINPUT_TRACE=1` prints every buffered DirectInput event.

Environment: `TM_D3D9_SHOTS=DIR` (with `--d3d9 dxvk`) writes the back buffer
as `DIR/frame_NNNNNN.ppm` before `Present` 1, 2, 4, 8... and every 1000th.
The frames contain the game's art: keep them out of the repository.

Exit codes: 0 reached `WinMain` (with `--stop-at-winmain`) or the program
exited normally; 2 a function that was not lifted; 3 a crash or a fatal
win32hle error; 4 the watchdog.

Expected for `--stop-at-winmain` on the 1.1 CD: `[bind] 50 imports are stubs`,
`[poc] WinMain 0x005303C0 entered: ... 12978 function entries, 1245 distinct functions`,
exit 0. Without it, and without `--d3d9 dxvk`, the run stops at the game's
`IDirect3D9::CreateDevice` call (exit 3): its parameters are printed as
`[d3d9]` lines (docs/D3D9-REQUIREMENTS.md). With `--d3d9 dxvk` the game
runs its intro and main menu until it exits or the watchdog stops it.

## 7. Regression test

```bash
python3 ~/tm/the-movies-android/tests/m2_winmain.py --host ~/tm/build/tm_host --game ~/tm/game --gen ~/tm/work/gen
```

Expected: `PASS` for both scenarios (`empty_user_dir_with_documents`, the
main one, and `empty_user_dir_without_documents`, a diagnostic). Each one
runs from the game directory with a fresh user directory and compares the
result with its own baseline in `tests/baselines/`, written with
`--scenario NAME --update`.

Shim tests (no game files needed):
- `test_disk_free_w`: `GetDiskFreeSpaceExW`;
- `test_loader`: the DLL loader and `GetTempPathA`;
- `test_dinput8`: DirectInput 8, with keyboard and mouse state and buffered data;
- `test_sysroot`: the Windows directory;
- `test_vfs`: the virtual file system and host DLLs, with the wide-character file functions;
- `test_vfs_audit`: file-system edge cases: capacity, allocation and
  handle-table failures, search lifecycle, `GetFileAttributesA` errors,
  wildcard rules, mount boundaries;
- `test_loader_audit`: loader edge cases: the reserved handle ranges (host
  DLLs and win32hle's system modules), handles that name no module, stale
  and malformed handles, reference counts, `GetProcAddress` by ordinal, COM
  objects in guest memory;
- `test_dshow`: the DirectShow start-up probe's objects: the probe's
  sequence, COM identity, reference ownership, the graph/filter relation,
  error results, and what must stop the run;
- `test_d3d9`: `d3d9.dll`, `Direct3DCreate9`, the device-free
  `IDirect3D9` methods, and what must stop the run;
- `test_winmm`: `waveInGetDevCapsA` with no wave input device;
- `test_ole32`: COM apartments (`CoInitialize`, `CoInitializeEx`,
  `CoUninitialize`) per thread;
- `test_d3d9_dxvk` (only with `TM_DXVK_PREFIX` and `xvfb-run`): the DXVK
  backend through guest objects: `CreateDevice`, COM identity, locks, a
  cleared frame read back, and the stops (DXVK-INTEGRATION.md 6);
- `dxvk_present` (only with `TM_DXVK_PREFIX` and `xvfb-run`): a real
  DXVK device with the game's device request, cleared, presented and read
  back (DXVK-INTEGRATION.md 3);
- `hle_path_selftest`: win32hle's own path selftest against the patched
  `hle_path.c`.

The build applies `patches/pcrecomp/*.patch` in order to copies of
`hle_path.c` and `hle_path_selftest.c` in the build tree (needs `patch`).
`0001` adds mounts and Windows path normalisation; `0002` changes the one
selftest expectation that `0001` changes on purpose. Configuring fails if a
patch does not apply to the pcrecomp revision in use.

```bash
ctest --test-dir ~/tm/build --output-on-failure
```

The run past WinMain, to the current execution boundary (docs/IMPORTS.md):
where and why it stops, the DirectInput calls and their results, the imports
called, the user directory it creates, and the game directory unchanged.

```bash
python3 ~/tm/the-movies-android/tests/m3_boundary.py --host ~/tm/build/tm_host --game ~/tm/game
```

Its baseline is `tests/baselines/m3_boundary.json`. It changes only with
`--update`, after the new boundary has been reviewed; the commit says why.
`--trace-out FILE` keeps the run's import trace.

`--imports-json FILE` writes every bound import with its status
(implemented, diagnostic, win32hle-present, win32hle-verified, unsupported).
`--import-trace FILE` records the calls the game makes (docs/IMPORTS.md).

Exception-handling structures and what a run reached (docs/EXCEPTIONS.md):

```bash
python3 -I ~/tm/the-movies-android/tools/eh_scan.py ~/tm/game/Movies.exe ~/tm/work/functions.json seen.txt
```
