# DXVK Native as the Direct3D 9 backend

The game's Direct3D 9 calls are to be answered by DXVK Native, which
implements Direct3D 9 on Vulkan:

```
game code (lifted, guest memory)
  -> guest IDirect3D9 / IDirect3DDevice9 objects (tm_d3d9.c, guest memory)
  -> host adapter (tm_d3d9.c: guest arguments converted to host ones)
  -> DXVK Native d3d9 (libdxvk_d3d9.so, host C++ objects)
  -> Vulkan -> GPU
```

This document describes the Linux x86-64 host integration with upstream
DXVK Native 3.1.1. Runtime observations below were made under lavapipe/Xvfb;
they do not describe the Android fork or establish Android compatibility.
The Android application uses a separate DXVK Native fork (see
[ANDROID-PORT-AUDIT.md](ANDROID-PORT-AUDIT.md)).

Tags: [src] read from the DXVK source at the pinned commit; [run]
measured here; [doc.] upstream documentation; [hyp.] not verified.

## 1. Version and licence

| | |
|---|---|
| Upstream | https://github.com/doitsujin/dxvk |
| Version | `v3.1.1`, commit `b1a1c99ab52b687cf950d62c88bc2fa316b41663` |
| Licence | zlib/libpng ([LICENSE](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/LICENSE)) |
| Components built | `d3d9` only (`dxgi`, `d3d8`, `d3d10`, `d3d11` off) |
| Window system | SDL2 (`-Dnative_sdl2=enabled`) |

The zlib licence permits linking and redistribution with this MIT
project. It asks that the origin is not misrepresented and that altered
sources are marked. No DXVK source is copied into this repository: DXVK is
built from its own checkout and linked as a shared library. A package that
ships `libdxvk_d3d9.so` carries DXVK's LICENSE with it.

The version is pinned. A newer DXVK is taken only as a deliberate step,
with the tests of section 6 rerun.

## 2. What DXVK Native is [src, doc.]

DXVK Native ([README, "DXVK Native"](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/README.md#dxvk-native))
is DXVK built as an ELF shared library for a non-Windows host, without
Wine. It exports `Direct3DCreate9` and implements the same C++ COM
interfaces as on Windows, for host code compiled against its headers
(`include/dxvk`, `pkg-config dxvk-d3d9`).

- **Window handles.** An `HWND` passed to DXVK is the window system's own
  window, here an `SDL_Window*`. DXVK creates its Vulkan surface with
  `SDL_Vulkan_CreateSurface`
  ([wsi_window_sdl2.cpp](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/wsi/sdl2/wsi_window_sdl2.cpp)),
  so the window must be created with `SDL_WINDOW_VULKAN`.
- **Window system choice.** It is not automatic: `DXVK_WSI_DRIVER=SDL2`
  must be set before the first DXVK call.
- **Full screen.** DXVK's SDL2 WSI enters full screen with
  `SDL_SetWindowFullscreen` and changes the mode with
  `SDL_SetWindowDisplayMode`.
- **Vulkan requirement.** The pinned v3.1.1 source checks for Vulkan API
  1.3 in `dxvk_instance.h` and applies additional mandatory device-feature
  and extension checks in `dxvk_device_info.cpp`:
  [version check](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_instance.h#L12),
  [compatibility check](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_device_info.cpp#L746),
  [required feature list](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_device_info.cpp#L802).
  The API-version check alone does not establish driver compatibility.
  DXVK's current driver guidance describes the 3.x feature set as Vulkan
  1.4-class and explicitly excludes first-party Android/mobile driver
  support: [upstream driver support](https://github.com/doitsujin/dxvk/wiki/Driver-support).
  The Android port uses a separately pinned Android DXVK Native fork; see
  [ANDROID-FEASIBILITY.md](ANDROID-FEASIBILITY.md).

## 3. Proof of concept: `tests/dxvk/dxvk_present.cpp` [run]

A standalone host program. It uses no game code and no guest memory, and
exercises DXVK's own `d3d9` with the game's device request
([D3D9-REQUIREMENTS.md](D3D9-REQUIREMENTS.md) 3). It:

1. opens an SDL2 window with `SDL_WINDOW_VULKAN`;
2. creates `IDirect3D9`, and prints the adapter, the mode counts for
   the game's display formats, and `CheckDeviceType` and
   `CheckDepthStencilMatch` for its request;
3. creates a HAL device with `D3DCREATE_HARDWARE_VERTEXPROCESSING |
   D3DCREATE_MULTITHREADED`, one back buffer, `DISCARD` and `D16` auto
   depth;
4. clears the back buffer to a colour, reads it back through
   `GetRenderTargetData` and checks it;
5. calls `Present`, reads the presented image back with
   `GetFrontBufferData` and checks it;
6. releases everything, checking that the reference counts reach 0.

It fails unless every step succeeds and both read-backs hold the colour.
CTest runs it as `dxvk_present` with the game's exact request: full
screen, 1024×768, `X1R5G5B5`.

**Environment of the measurement.** Ubuntu 24.04 x86-64 in a container
with no GPU. Vulkan was Mesa 25.2.8 lavapipe (`llvmpipe`, a software Vulkan
1.3 driver) under Xvfb (`xvfb-run -s "-screen 0 1024x768x24"`). SDL 2.30.0,
GCC 13.3, meson 1.12.1.

**Results** (`dxvk_present 1024 768 24 0`):

| Check | Result |
|---|---|
| `Direct3DCreate9(D3D_SDK_VERSION)` | an object |
| `GetAdapterIdentifier(0)` | `AMD Radeon RX 6700 XT` (`aticfx64.dll`): DXVK reports a spoofed vendor for the software device |
| `GetAdapterModeCount` | `X8R8G8B8`: 1 (Xvfb's single mode); `R5G6B5`: 0; `X1R5G5B5`: 0 |
| `CheckDeviceType(X8R8G8B8 display, X1R5G5B5 back, full screen)` | not `D3D_OK` |
| `CreateDevice(1024×768 X1R5G5B5, full screen, D16, 0x44)` | `D3D_OK` |
| back buffer | 1024×768 `X1R5G5B5`; centre pixel `0x9197` = (4, 12, 23) for clear colour (0x20, 0x60, 0xC0), as expected after rounding to 5 bits |
| `Present` | `D3D_OK` |
| `GetFrontBufferData` | centre pixel `0xFF2163BD`: the colour, within 16-bit precision |
| releases | device and `IDirect3D9` reach 0 |

The run took 2.1 s. The same program passes with windowed `X8R8G8B8`.

**What this shows.**
- The game's device request can be created and presented by DXVK, at least
  on lavapipe.
- DXVK's `CheckDeviceType` and its `CreateDevice` disagree about a 16-bit
  full-screen back buffer on a 32-bit display: the check says no, the
  device works.

**What this does not show.**
- That DXVK reports 16-bit display modes. It reports none on Xvfb, and the
  game's 16-bit pass needs one (D3D9-REQUIREMENTS.md 2). A backend that
  forwards `GetAdapterModeCount` to DXVK would change which formats the game
  picks. Section 4 keeps this decision explicit.
- Anything about a real GPU, or about Android.

## 4. Guest/host ABI and object model

1. **Guest objects stay in guest memory.** The game sees only the objects
   `tm_d3d9.c` allocates with `hle_alloc`, with shim vtables. Each one maps,
   in a host table keyed by its guest address, to the DXVK object it
   stands for. No host pointer is written to guest memory.
2. **Structures are converted.** Structures that hold a pointer or a
   handle differ between the 32-bit guest and the 64-bit host
   (`D3DPRESENT_PARAMETERS.hDeviceWindow`, `D3DLOCKED_RECT.pBits`), and are
   rewritten field by field. Pointer-free structures (`D3DDISPLAYMODE`,
   `D3DSURFACE_DESC`, `D3DCAPS9`) have the same layout on both sides, and
   DXVK reads and writes them in guest memory; each size is checked at
   compile time against the 32-bit layout (section 5).
3. **Locked memory.** `Lock` returns a pointer the guest can read, so it
   cannot be DXVK's host pointer. The guest gets a shadow in guest memory,
   written back on `Unlock` unless the lock is `READONLY`. For textures and
   surfaces it is filled from DXVK unless the lock is `DISCARD`; for buffers
   it persists, since the guest is their only writer (section 5).
4. **Window.** The guest `HWND` (`0x00010010`) is win32hle's window. The
   host `HWND` DXVK gets is the `SDL_Window*` win32hle created for it,
   which must be created with Vulkan support (win32hle's screen in Vulkan
   mode).
5. **Queries.** `IDirect3D9` queries go to DXVK where the answer describes
   the backend: caps, formats, device checks. Where DXVK's answer depends on
   the host desktop (display modes), the mismatch with win32hle's
   `GetSystemMetrics` desktop is resolved explicitly and recorded in
   [D3D9-REQUIREMENTS.md](D3D9-REQUIREMENTS.md). It is never left to
   chance.
6. **Parameters are never silently changed.** `CreateDevice` passes the
   game's parameters as given. If DXVK refuses them, the run stops with
   DXVK's result and the parameters. Any deliberate change, such as
   windowed instead of full screen for development, is a named option and
   is logged.
7. **Threads.** The game asks for `D3DCREATE_MULTITHREADED`. DXVK runs its
   own submission and compiler threads. Guest calls into it are
   serialised by win32hle's machine lock. DXVK's threads never run guest
   code.
8. **Method status.** Each guest method is one of: implemented and tested;
   implemented with stated limitations; a diagnostic stop that logs its
   arguments and caller; unsupported. [D3D9-REQUIREMENTS.md](D3D9-REQUIREMENTS.md)
   6 carries the status.

## 5. The backend in the host: `--d3d9 dxvk` [run]

`tm_host ... --d3d9 dxvk` opens win32hle's window in Vulkan mode and sets
`src/host/tm_d3d9_dxvk.cpp` as the device backend of `tm_d3d9.c`. Without
the option the run is headless, and the game's `CreateDevice` stops it as
before (`tests/m3_boundary.py`).

**How a guest call reaches DXVK.**
- `tm_d3d9.c` owns the guest `IDirect3D9`. Its `CreateDevice` hands the
  call to the backend. The backend creates one DXVK `IDirect3D9` per guest
  `IDirect3D9`, on first use, and forwards the queries that describe the
  backend: `CheckDeviceFormat`, `CheckDeviceType`, `CheckDepthStencilMatch`,
  `CheckDeviceMultiSampleType`, `GetDeviceCaps` and
  `GetAdapterIdentifier`. Mode enumeration follows the display-mode mapping described in section 4.
- **Objects.** Every DXVK object the guest holds has one guest object: 16
  bytes of guest memory, its first dword a vtable of shim VAs. The vtables
  come from `src/host/tm_d3d9_ifaces.h`, which lists all 14 interfaces in
  vtable order with their argument counts.
  - Two tables map guest address to DXVK object and back, so the same DXVK
    object reaches the guest as the same address.
  - Reference counts are DXVK's.
  - A texture's levels count their references on the texture, as on
    Windows. Their guest objects are freed with the texture.
- **Forwarding.** Most methods go through one template, `Fwd<&I::Method>`,
  which converts each argument by its host type:
  - integers, enums and `BOOL` pass as the dword; `float` passes as its
    bits;
  - a pointer to a pointer-free structure passes as the guest address,
    which the host reads and writes in place;
  - an interface pointer passes as the live guest object's DXVK object;
  - an interface out-pointer is filled with the guest object;
  - a shared-resource `HANDLE *` must be NULL.

  Each structure's size is checked at compile time against its 32-bit
  layout (`D3DCAPS9` 304 bytes, `D3DLIGHT9` 104, `D3DVOLUME_DESC` 28,
  `D3DINDEXBUFFER_DESC` 20...). The argument count is checked against the
  vtable table. A type with no conversion does not compile.
- **Locks.** `D3DLOCKED_RECT` and `Lock`'s `void **` carry pointers, so
  these methods are written by hand:
  - **Vertex and index buffers** get a persistent shadow in guest memory.
    The guest is the only writer of a buffer, so the shadow always holds
    what DXVK holds. `Lock` returns the shadow. `Unlock` copies the locked
    range into DXVK under a DXVK lock with the guest's `DISCARD` or
    `NOOVERWRITE`. A `READONLY` lock copies from DXVK first and nothing
    back.
  - **Textures and surfaces.** `LockRect` locks DXVK's surface and keeps it
    locked. The guest gets a shadow with DXVK's pitch, filled from DXVK
    unless the lock is `DISCARD`. `UnlockRect` copies the rectangle's rows
    back unless the lock is `READONLY`. Block formats (`DXT1`–`DXT5`) are
    copied in 4×4 blocks.
  - A second lock before the unlock, or an unknown flag, stops the run.
- **`Present`.** The window override and dirty region must be NULL,
  otherwise the run stops. After presenting, win32hle is told where the
  picture is, so the mouse maps onto it.
- **Mouse.** The game reads the mouse through DirectInput (non-exclusive:
  its `SetCooperativeLevel` with a NULL window fails with `E_HANDLE`, as
  on Windows, and it acquires the device anyway), moves its own cursor by
  the relative movement times its sensitivity setting (about 1.5 measured),
  and calls `SetCursorPos(512, 384)` each frame. win32hle counts movement
  without the `SetCursorPos` jumps, as Windows does. Two win32hle changes
  were needed in a window without an SDL renderer
  (`patches/pcrecomp/0003-screen-mouse-in-gl-window.patch`):
  - the mouse is held in the window there too;
  - `SetCursorPos` moves the host's pointer by the picture `Present`
    reported, since a Direct3D game sets no DirectDraw mode. Before, the
    pointer stayed where it was, and every movement counted from the
    centre again, so the game's cursor ran to an edge.
- **Full screen.** A full-screen device sets the guest's display size
  (`hle_set_screen_size`), as a mode change does on Windows.
- **Not implemented.** Every other method is a diagnostic stop. It prints
  the interface, the method, the arguments as the guest passed them and
  the lifted function that called it.
- **Diagnostics.** `TM_D3D9_SHOTS=<dir>` writes the back buffer before
  `Present` number 1, 2, 4, 8... and every 1000th, as PPM. The report at
  exit counts the calls of every forwarded method.

**What the game does with it** (one run, lavapipe, Xvfb 1024×768, about 60
seconds, `docs/traces/dxvk_run_imports.json` for the imports):
- `CreateDevice` with the game's exact parameters succeeds.
- The game then:
  - asks for the device caps and checks five texture formats;
  - creates 304 vertex buffers (4 MB, write-only), 135 index buffers
    (1 MB each) and its textures (DXT1/DXT3 among them);
  - starts its intro and main menu.
- Its own rendering code draws:
  - the animated Lionhead Studios logo, from frame 1;
  - then the main menu: the logo, six items in the Russian edition's text,
    and the cursor, by frame 3000.
- The menus respond to the mouse: `tools/xvfb_clicks.sh` clicks
  "Играть", then "Загрузить игру", and the game shows its load screen with
  its backdrop and an empty list (no saves yet). "Играть", "Новая игра"
  loads the game proper: the studio lot in 3D, with the tutorial's first
  message, and the run goes on (3 minutes measured, no stop). "Посмотреть
  фильм" opens the film viewer with the sample film in its list.
- The menu's backdrop is black. It is a video, and Windows Media playback
  is not implemented (DIRECTSHOW-STARTUP.md).
- About 60 frames a second through lavapipe.
- The pipeline is fixed-function:
  - `SetFVF` and `DrawIndexedPrimitive` with `SetStreamSource` and
    `SetIndices`, about 280,000 draws;
  - lights and materials;
  - texture stage and render states;
  - `SetVertexShader` and `SetPixelShader` called only with NULL; no
    shader is created.

**Limitations, stated.**
- **Display modes.** `IDirect3D9`'s mode list and `GetAdapterDisplayMode`
  describe win32hle's display, not DXVK's. `GetAdapterDisplayMode` keeps
  answering `X8R8G8B8` after the 16-bit full-screen device exists.
- **No `D3DCREATE_FPU_PRESERVE`.** Windows would set the calling thread's
  x87 precision to 24 bits. The guest's x87 state is not changed. The
  run logs this.
- **Buffer lock cost.** Every buffer `Unlock` copies the locked range into
  DXVK: about 280,000 copies a minute in the menu. Fine on x86-64; a
  measured cost on ARM64 is open.
- `GetProcAddress(d3d9.dll, "DebugSetMute")` is asked about 80 times a
  second and answered NULL (not exported here). The retail `d3d9.dll`
  exports it; whether the game uses it when present is not checked.

## 6. Building and testing

[BUILD.md](BUILD.md) 5.1 gives the commands. In short: build DXVK with meson
into a prefix, then configure this project with
`-DTM_DXVK_PREFIX=<prefix>`. That adds the `dxvk_present` program and the
backend in `tm_host`. When `xvfb-run` is installed, it also adds two CTests:
`dxvk_present`, and `test_d3d9_dxvk` (`tests/c/test_d3d9_dxvk.c`: the
backend through guest objects only, 55 checks). The second covers:
- the forwarded `IDirect3D9` queries;
- `CreateDevice` on a guest window, with the parameters written back;
- COM identity, `QueryInterface`, reference counts, `GetDirect3D` and
  `GetDevice`;
- buffer and texture locks, a rectangle and DXT1 included;
- a level freed with its texture;
- a frame cleared and read back through `GetRenderTargetData`;
- the stops: an unimplemented method, a `this` that is no device, a method
  after the last `Release`, a second lock.

The unit tests present at commit `98dc656` passed under AddressSanitizer and
UndefinedBehaviorSanitizer (GCC 13); `test_winmm` and patch 0003 came later
and have not been run under them.

Without `TM_DXVK_PREFIX` nothing DXVK-related is built. The rest of the
build and its tests do not depend on DXVK.
