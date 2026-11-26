# Direct3D 9: what the game asks for

This is the measurement that chooses the Direct3D 9 backend (AUDIT.md 5.3,
M3.md 3.4). It holds only what the game was seen to do, or what was read
from its code. The runtime measurements below are from the Linux x86-64
DXVK Native run under lavapipe/Xvfb; they are not measurements of the
Android D3D9 probe or proof that the full game runs on Android. It grows as
the run gets further.

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

## 4. After `CreateDevice` [run]

Measured with the DXVK backend (DXVK-INTEGRATION.md 5), one run of about
60 seconds through the intro and into the main menu, lavapipe under Xvfb.
Counts are calls of the guest methods.

- **Caps.** `GetDeviceCaps` on the device, 705 times; none on
  `IDirect3D9` before `CreateDevice`. DXVK on lavapipe reports vertex and
  pixel shader 3.0 and 16384×16384 textures. Which caps the game branches
  on is not traced.
- **Formats.** `CheckDeviceFormat` for `X1R5G5B5` (24), `A4R4G4B4` (26),
  `L8` (50), `A8L8` (51) and `V8U8` (60). The textures it creates also use
  `DXT1` and `DXT3`, which it does not check first.
- **Resources.**
  - 366 textures, DXT1 and DXT3 among them; their levels reached through
    `GetSurfaceLevel` (1,709) and locked (`LockRect`: 161 on textures,
    2,046 on surfaces);
  - 304 vertex buffers and 135 index buffers, created at start;
  - 4 render targets, used with `StretchRect` and `SetRenderTarget`;
  - no volume or cube textures, no queries, no state blocks, no vertex
    declarations.
- **Shaders.** None. `SetVertexShader` and `SetPixelShader` are called
  1,291 times each, with NULL. The intro and the menu use the
  fixed-function pipeline.
- **Per frame.** `TestCooperativeLevel` polled (570,182 calls),
  `BeginScene`/`EndScene`, `Clear` and `Present` once a frame (3,648
  frames). Per draw: `SetFVF`, `SetTexture`, `SetStreamSource`,
  `SetIndices`, a vertex-buffer `Lock`/`Unlock`, then
  `DrawIndexedPrimitive` (285,089). Also 7,746 `DrawIndexedPrimitiveUP`,
  114,450 `SetRenderState`, 107,728 `SetTextureStageState`, 2,582
  `SetScissorRect`, 190 `SetLight`/`LightEnable`, 375 `SetTransform`.
- **The game proper** (a new game, the studio lot, measured over 3
  minutes): the same methods and still no shader; `SetVertexShader` and
  `SetPixelShader` only with NULL. It enumerates texture formats with
  `CheckDeviceFormat` (usage 0, `D3DRTYPE_TEXTURE`) over and over, about
  23,600 calls: DXVK on lavapipe answers `D3DERR_NOTAVAILABLE` for
  `R8G8B8` (20), `R3G3B2` (27), `A8R3G3B2` (29), `A8P8` (40), `P8` (41),
  `A4L4` (52) and format 117, and `D3D_OK` for the others it asks
  (21–26, 28, 30–36, 50, 51, 60–64, 67, 81, 110, the DXT FOURCCs and three
  other FOURCCs). What it does with the answers is not traced.
- **Not reached yet:** a lost device and `Reset`; the video path, which the
  menu's backdrop needs; a full year on the lot.

## 5. What tm_d3d9.c answers, and why

`IDirect3D9` answers only what needs no device. It reports one adapter,
the desktop mode as `GetSystemMetrics` reports it, and the DirectDraw
mode list. `CreateDevice` records its arguments and stops the run. Every
other method is a stub that stops the run, naming itself. Nothing claims
a capability that no backend provides.

## 6. Method status in the DXVK backend

Every method of the 14 interfaces in `src/host/tm_d3d9_ifaces.h` has one of
these states. The list of implemented methods is `k_impl` in
`src/host/tm_d3d9_dxvk.cpp`.

| State | Methods |
|---|---|
| implemented and tested (`test_d3d9_dxvk`, and called by the game) | `IUnknown` on every interface; device `CreateDevice`, `GetDeviceCaps`, `GetDirect3D`, `Present`, `Clear`, `CreateTexture`, `CreateVertexBuffer`, `CreateIndexBuffer`, `GetRenderTarget`; buffer `Lock`/`Unlock`; texture and surface `LockRect`/`UnlockRect`; `GetSurfaceLevel`; `GetDevice`; `IDirect3D9` `CheckDeviceFormat`, `CheckDeviceType`, `GetDeviceCaps`, `GetAdapterIdentifier` |
| implemented, called by the game, not unit-tested | the rest of the per-frame calls in section 4: scene, state, transform, light, texture-stage, sampler, scissor, stream, FVF and draw methods; `CreateRenderTarget`, `StretchRect`, `SetRenderTarget`, `GetDisplayMode`, `GetAvailableTextureMem`, `TestCooperativeLevel` |
| implemented with limitations | buffer and texture locks: one lock at a time per resource; `Present`: no window override and no dirty region; `CreateDevice`: one window (device and focus window the same), no `FPU_PRESERVE` emulation (DXVK-INTEGRATION.md 5) |
| forwarded by the generic template, not yet called | the other device methods in `k_impl` (shader, declaration, state-block, query, cube and volume creation; their getters), and the resource methods of those interfaces. Their argument types are checked at compile time; their behaviour is DXVK's |
| diagnostic stop | `GetCreationParameters`, `Reset`, `CreateAdditionalSwapChain`, `GetNPatchMode`, `ProcessVertices`, `DrawRectPatch`, `DrawTriPatch`, `DeletePatch`; every swap-chain, volume and volume-texture method except `GetDevice`; the rest not in `k_impl`. A stop prints the method, its arguments and the calling function |

