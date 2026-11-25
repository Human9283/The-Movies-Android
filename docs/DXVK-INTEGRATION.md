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
with the tests of section 5 rerun.

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
- **Vulkan requirement.** DXVK 3.1.1 requires a Vulkan 1.3 device
  ([dxvk_instance.h](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_instance.h#L12)).
  It also requires a fixed feature list, and rejects any adapter missing one
  of them
  ([checkDeviceCompatibility](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_device_info.cpp#L746),
  [getFeatureList](https://github.com/doitsujin/dxvk/blob/b1a1c99ab52b687cf950d62c88bc2fa316b41663/src/dxvk/dxvk_device_info.cpp#L802)).
  This list decides Android feasibility:
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

## 4. Integration decisions

1. **Guest objects stay in guest memory.** The game sees only the objects
   `tm_d3d9.c` allocates with `hle_alloc`, with shim vtables. Each one maps,
   in a host table keyed by its guest address, to the DXVK object it
   stands for. No host pointer is written to guest memory.
2. **Structures are converted.** Structures that hold a pointer or a
   handle differ between the 32-bit guest and the 64-bit host
   (`D3DPRESENT_PARAMETERS.hDeviceWindow`, `D3DLOCKED_RECT.pBits`), and are
   rewritten field by field. Pointer-free structures (`D3DDISPLAYMODE`,
   `D3DSURFACE_DESC`, `D3DCAPS9`) have the same layout on both sides, and
   are still copied field by field.
3. **Locked memory.** `Lock` returns a pointer the guest can read, so it
   cannot be DXVK's host pointer. The planned scheme is a guest-memory
   shadow of the locked range: filled from DXVK's mapping unless the lock
   is `DISCARD` or write-only, and written back on `Unlock` unless it is
   `READONLY`.
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
   arguments and caller; unsupported. [IMPORTS.md](IMPORTS.md) and
   [D3D9-REQUIREMENTS.md](D3D9-REQUIREMENTS.md) carry the status.

## 5. Building and testing

[BUILD.md](BUILD.md) 5.1 gives the commands. In short: build DXVK with meson
into a prefix, then configure this project with
`-DTM_DXVK_PREFIX=<prefix>`. That adds the `dxvk_present` program, and,
when `xvfb-run` is installed, the `dxvk_present` CTest.

Without `TM_DXVK_PREFIX` nothing DXVK-related is built. The rest of the
build and its tests do not depend on DXVK.
