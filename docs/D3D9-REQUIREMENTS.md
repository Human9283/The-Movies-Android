# Direct3D 9: what the game asks for

This is the measurement that chooses the Direct3D 9 backend (AUDIT.md 5.3,
M3.md 3.4). It holds only what the game was seen to do, or what was read
from its code. It grows as the run gets further.

Tags: [static] read from `Movies.exe` 1.1; [run] measured on the M3 run
(`tests/m3_boundary.py`, trace `docs/traces/m3_boundary_imports.json`);
[doc.] Microsoft documentation; [hyp.] not verified.

## 1. Where it starts [static, run]

`sub_0093D780`, the graphics start-up, is called with the main window. It
stores the window at `0x00FA7128` (the handle is `0x00010010`, the window
`WinMain` created). Then it calls, in order:

| # | Call | Where | Result here |
|---|---|---|---|
| 1 | `Direct3DCreate9(32)` (`D3D_SDK_VERSION`), delay-loaded from `d3d9.dll` (thunk `0x00A5A95E`) | `0x0093D816`; the object is stored at `0x00FA7CB8` | an `IDirect3D9` |
| 2 | `IDirect3D9::GetAdapterDisplayMode(D3DADAPTER_DEFAULT, 0x00FA7CC0)` (vtable `+0x20`) | `0x0093D831`; a failure ends the start-up | `640x480`, refresh 0, `X8R8G8B8` |
| 3 | `sub_0093D700`: two passes of `sub_0093D600` over lists of formats, each `GetAdapterModeCount(0, format)` (`+0x18`) then `EnumAdapterModes(0, format, i)` (`+0x1C`) for every mode, filtered by `sub_0093B050` | `0x0093D651`, `0x0093D673` | see 2 |
| 4 | `IDirect3D9::CreateDevice` (`+0x40`) | from `sub_0093D780` | stops the run: no device (section 3) |

No `GetDeviceCaps`, `GetAdapterIdentifier`, `CheckDeviceType` or
`CheckDeviceFormat` comes before `CreateDevice` on this path.

## 2. Formats and modes [static, run]

`sub_0093D700` builds two format lists on its stack and passes each to
`sub_0093D600`, with a result record (`0x00FA709C`, then `0x00FA70B4`):

| Pass | Formats, in order | Record |
|---|---|---|
| 16-bit | `A1R5G5B5` (25), `X1R5G5B5` (24), `R5G6B5` (23), `A4R4G4B4` (26), `X4R4G4B4` (30) | `0x00FA709C` |
| 32-bit | `A8R8G8B8` (21), `X8R8G8B8` (22), `R8G8B8` (20) | `0x00FA70B4` |

A pass stops at the first format whose modes pass the game's filter
(`sub_0093B050`, then `sub_0093D550`). The queries seen were:

| `GetAdapterModeCount` format | Answer here |
|---|---|
| 25 `A1R5G5B5` | 0 |
| 24 `X1R5G5B5` | 23 (equivalent to 565, per the documentation) |
| 21 `A8R8G8B8` | 0 |
| 22 `X8R8G8B8` | 23 |

So the game keeps one 16-bit and one 32-bit display format. Which
formats a real driver reports for `A1R5G5B5` and `A8R8G8B8` varies [hyp.].
The answers here are those of tm_d3d9.c (0: not display formats).

The 23 modes are win32hle's DirectDraw table (`hle_dd_modes`), so
DirectDraw and Direct3D 9 report the same modes.

## 3. The device the game asks for [run]

```
CreateDevice(adapter 0, D3DDEVTYPE_HAL, focus 0x00010010,
             D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED (0x44))
BackBufferWidth            1024
BackBufferHeight           768
BackBufferFormat           X1R5G5B5 (24)
BackBufferCount            1
MultiSampleType            0 (none)
MultiSampleQuality         0
SwapEffect                 1 (D3DSWAPEFFECT_DISCARD)
hDeviceWindow              0 (the focus window)
Windowed                   0 (full screen)
EnableAutoDepthStencil     1
AutoDepthStencilFormat     D16 (80)
Flags                      0
FullScreen_RefreshRateInHz 0 (default)
PresentationInterval       0 (D3DPRESENT_INTERVAL_DEFAULT)
```

What this implies for a backend:
- **Hardware vertex processing** is requested, so the game will use vertex
  buffers and the fixed-function or shader pipeline through the device.
  Which one is not known yet.
- **`D3DCREATE_MULTITHREADED`**: the game expects to call the device from
  more than one thread. Under win32hle's machine lock only one guest
  thread runs at a time, so the backend must not depend on the calling
  thread.
- **A 16-bit back buffer** (`X1R5G5B5`) with a 16-bit depth buffer at
  1024×768. It is the first of its 16-bit formats with modes, and the size
  is not the desktop's (640×480 here). [hyp.] It comes from the game's
  settings, which are their defaults on this first launch.
- **Full screen and `DISCARD`**: one back buffer, no copy semantics to
  preserve.

## 4. Not known yet

- Shader versions and the caps it checks (no `GetDeviceCaps` before
  `CreateDevice`; it may query the device's caps after).
- Texture formats, pools and the resources it creates.
- How it behaves on a lost device.

Each one is answered when the run gets past `CreateDevice`, which needs a
device backend (M3 steps 5–6).

## 5. What tm_d3d9.c answers, and why

`IDirect3D9` answers only what needs no device. It reports one adapter,
the desktop mode as `GetSystemMetrics` reports it, and the DirectDraw
mode list. `CreateDevice` records its arguments and stops the run. Every
other method is a stub that stops the run, naming itself. Nothing claims
a capability that no backend provides.
