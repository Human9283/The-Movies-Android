/*
 * DXVK Native proof of concept (docs/DXVK-INTEGRATION.md): a real Direct3D 9
 * device on Vulkan, through DXVK's own d3d9, with no game code and no guest
 * memory. It:
 *   - opens an SDL2 window with Vulkan support (DXVK's SDL2 WSI);
 *   - reports which of The Movies' formats DXVK accepts
 *     (CheckDeviceType/CheckDepthStencilMatch for its CreateDevice request);
 *   - creates a HAL device with the game's flags
 *     (HARDWARE_VERTEXPROCESSING | MULTITHREADED), D16 auto depth, DISCARD;
 *   - clears the back buffer to a colour, reads it back through
 *     GetRenderTargetData, presents, and reads the presented image back with
 *     GetFrontBufferData;
 *   - fails unless both read-backs hold the colour.
 *
 *   DXVK_WSI_DRIVER=SDL2 xvfb-run -a ./dxvk_present [WIDTH HEIGHT FORMAT WINDOWED]
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <d3d9.h>

static int g_fail;
static void check(const char *what, bool ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}
static const char *fmt(D3DFORMAT f) {
    switch (f) {
        case D3DFMT_X8R8G8B8: return "X8R8G8B8"; case D3DFMT_A8R8G8B8: return "A8R8G8B8";
        case D3DFMT_R5G6B5: return "R5G6B5";     case D3DFMT_X1R5G5B5: return "X1R5G5B5";
        case D3DFMT_A1R5G5B5: return "A1R5G5B5"; case D3DFMT_D16: return "D16";
        default: return "?";
    }
}

int main(int argc, char **argv) {
    UINT w = argc > 2 ? (UINT)atoi(argv[1]) : 1024, h = argc > 2 ? (UINT)atoi(argv[2]) : 768;
    D3DFORMAT bb = argc > 3 ? (D3DFORMAT)atoi(argv[3]) : D3DFMT_X8R8G8B8;
    BOOL windowed = argc > 4 ? atoi(argv[4]) : TRUE;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("FAIL SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("dxvk_present", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, (int)w, (int)h,
                                       SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    check("SDL window with Vulkan", win != nullptr);
    if (!win) { printf("  %s\n", SDL_GetError()); return 1; }

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    check("Direct3DCreate9(D3D_SDK_VERSION)", d3d != nullptr);
    if (!d3d) return 1;
    D3DADAPTER_IDENTIFIER9 id = {};
    if (SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &id))) printf("     adapter 0: %s (%s)\n", id.Description, id.Driver);
    printf("     adapters: %u\n", d3d->GetAdapterCount());

    /* what DXVK says about the game's request (docs/D3D9-REQUIREMENTS.md 3) */
    static const D3DFORMAT disp[] = { D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5 };
    static const D3DFORMAT back[] = { D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A1R5G5B5 };
    for (D3DFORMAT a : disp) {
        printf("     GetAdapterModeCount(%s) = %u\n", fmt(a), d3d->GetAdapterModeCount(0, a));
        for (D3DFORMAT b : back)
            for (int wnd = 0; wnd < 2; wnd++) {
                HRESULT hr = d3d->CheckDeviceType(0, D3DDEVTYPE_HAL, a, b, wnd);
                if (hr == D3D_OK) printf("     CheckDeviceType(display %s, back %s, %s) = D3D_OK\n", fmt(a), fmt(b), wnd ? "windowed" : "full screen");
            }
    }
    HRESULT ds = d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, bb, D3DFMT_D16);
    printf("     CheckDepthStencilMatch(%s, D16) = 0x%08X\n", fmt(bb), (unsigned)ds);

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = w, pp.BackBufferHeight = h, pp.BackBufferFormat = bb, pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = nullptr, pp.Windowed = windowed;
    pp.EnableAutoDepthStencil = TRUE, pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, (HWND)win,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &pp, &dev);
    printf("     CreateDevice(%ux%u %s, %s) = 0x%08X\n", w, h, fmt(bb), windowed ? "windowed" : "full screen", (unsigned)hr);
    check("CreateDevice with the game's flags", SUCCEEDED(hr) && dev);
    if (!dev) return 1;

    const D3DCOLOR colour = D3DCOLOR_XRGB(0x20, 0x60, 0xC0);
    check("Clear", SUCCEEDED(dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colour, 1.0f, 0)));

    /* the back buffer, read back before Present */
    IDirect3DSurface9 *rt = nullptr, *sys = nullptr;
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &rt);
    D3DSURFACE_DESC d = {};
    rt->GetDesc(&d);
    check("back buffer has the requested size and format", d.Width == w && d.Height == h && d.Format == bb);
    dev->CreateOffscreenPlainSurface(w, h, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    check("GetRenderTargetData", sys && SUCCEEDED(dev->GetRenderTargetData(rt, sys)));
    D3DLOCKED_RECT lr = {};
    bool back_ok = false;
    if (sys && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        const uint8_t *row = (const uint8_t *)lr.pBits + (size_t)lr.Pitch * (h / 2);
        if (d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8) {
            uint32_t px; memcpy(&px, row + 4 * (w / 2), 4);
            printf("     back buffer centre pixel 0x%08X\n", px);
            back_ok = (px & 0xFFFFFF) == (colour & 0xFFFFFF);
        } else {
            uint16_t px; memcpy(&px, row + 2 * (w / 2), 2);
            /* per channel, within one step: the clear colour is converted from float with rounding */
            int r, g, bl, gmax;
            if (d.Format == D3DFMT_R5G6B5) r = px >> 11, g = (px >> 5) & 63, bl = px & 31, gmax = 63;
            else r = (px >> 10) & 31, g = (px >> 5) & 31, bl = px & 31, gmax = 31;      /* X1R5G5B5: bit 15 ignored */
            int wr = (0x20 * 31 + 127) / 255, wg = (0x60 * gmax + 127) / 255, wb = (0xC0 * 31 + 127) / 255;
            printf("     back buffer centre pixel 0x%04X: %d %d %d (want %d %d %d)\n", px, r, g, bl, wr, wg, wb);
            back_ok = abs(r - wr) <= 1 && abs(g - wg) <= 1 && abs(bl - wb) <= 1;
        }
        sys->UnlockRect();
    }
    check("the back buffer holds the clear colour", back_ok);

    check("Present", SUCCEEDED(dev->Present(nullptr, nullptr, nullptr, nullptr)));

    /* the presented image: GetFrontBufferData wants an A8R8G8B8 system-memory surface of the display's size */
    D3DDISPLAYMODE dm = {};
    dev->GetDisplayMode(0, &dm);
    IDirect3DSurface9 *front = nullptr;
    dev->CreateOffscreenPlainSurface(dm.Width, dm.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &front, nullptr);
    hr = front ? dev->GetFrontBufferData(0, front) : E_FAIL;
    printf("     GetFrontBufferData (%ux%u) = 0x%08X\n", dm.Width, dm.Height, (unsigned)hr);
    bool front_ok = false;
    if (SUCCEEDED(hr) && SUCCEEDED(front->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        uint32_t px; memcpy(&px, (const uint8_t *)lr.pBits + (size_t)lr.Pitch * (h / 2) + 4 * (w / 2), 4);
        printf("     front buffer centre pixel 0x%08X\n", px);
        /* 16-bit back buffers lose the low bits */
        int dr = (int)((px >> 16) & 0xFF) - 0x20, dg = (int)((px >> 8) & 0xFF) - 0x60, db = (int)(px & 0xFF) - 0xC0;
        front_ok = abs(dr) <= 8 && abs(dg) <= 8 && abs(db) <= 8;
        front->UnlockRect();
    }
    check("the presented frame holds the clear colour", front_ok);

    if (front) front->Release();
    if (sys) sys->Release();
    rt->Release();
    ULONG left = dev->Release();
    check("device released to 0", left == 0);
    check("IDirect3D9 released to 0", d3d->Release() == 0);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("dxvk_present: %s\n", g_fail ? "FAILED" : "PASSED");
    return g_fail ? 1 : 0;
}
