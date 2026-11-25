/*
 * tm_d3d9_backend.h - the device backend behind tm_d3d9.c's IDirect3D9
 * (docs/DXVK-INTEGRATION.md 4). tm_d3d9.c owns the guest IDirect3D9 objects;
 * a backend creates devices for them. With no backend set, CreateDevice
 * records its arguments and stops the run.
 *
 * Every argument is a guest value: addresses are guest VAs, the window is a
 * guest HWND. A backend never writes a host pointer into guest memory.
 */
#ifndef TM_D3D9_BACKEND_H
#define TM_D3D9_BACKEND_H
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct tm_d3d9_backend {
    const char *name;
    /* IDirect3D9::CreateDevice for the guest IDirect3D9 `d3d`: pp is the
     * guest D3DPRESENT_PARAMETERS (read, and written back as Direct3D 9
     * does), out the guest IDirect3DDevice9 ** (already checked non-NULL
     * and set to NULL). Returns the HRESULT the guest gets. */
    uint32_t (*create_device)(uint32_t d3d, uint32_t adapter, uint32_t type, uint32_t focus, uint32_t flags,
                              uint32_t pp, uint32_t out);
    /* the guest IDirect3D9 `d3d` was destroyed */
    void (*d3d_destroyed)(uint32_t d3d);
    void (*report)(FILE *f);
} tm_d3d9_backend;

/* Sets the backend. Its IDirect3D9 methods (shims named
 * "IDirect3D9::<method>", registered before the first Direct3DCreate9) take
 * the place of the stubs for what describes the backend: format and device
 * checks, caps, the adapter's identity. */
void tm_d3d9_set_backend(const tm_d3d9_backend *b);
/* the guest IDirect3D9 at `va` is alive (tm_d3d9.c) */
int tm_d3d9_is_d3d(uint32_t va);
/* IDirect3D9::AddRef of the guest IDirect3D9 at `va`, for a method that
 * returns it (IDirect3DDevice9::GetDirect3D) */
uint32_t tm_d3d9_addref(uint32_t va);

#ifdef __cplusplus
}
#endif
#endif
