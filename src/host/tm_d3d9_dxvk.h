/*
 * tm_d3d9_dxvk.h - the DXVK Native device backend (tm_d3d9_dxvk.cpp), built
 * only with TM_DXVK_PREFIX (docs/DXVK-INTEGRATION.md).
 */
#ifndef TM_D3D9_DXVK_H
#define TM_D3D9_DXVK_H
#include "tm_d3d9_backend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* registers IDirect3DDevice9's methods; the backend for tm_d3d9_set_backend */
const tm_d3d9_backend *tm_d3d9_dxvk_backend(void);
/* guest objects of a kind (tm_d3d9_ifaces.h: 0 devices, 2 surfaces...) alive now */
int tm_d3d9_dxvk_live(int kind);
#ifdef __cplusplus
}
#endif
#endif
