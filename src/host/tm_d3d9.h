#ifndef TM_D3D9_H
#define TM_D3D9_H
#include <stdio.h>

/* Registers d3d9.dll as a host DLL in the system directory, with
 * Direct3DCreate9 and the IDirect3D9 methods that need no device
 * (tm_d3d9.c). */
void tm_d3d9_init(void);
/* IDirect3D9 objects alive now. */
int  tm_d3d9_live(void);
void tm_d3d9_report(FILE *f);

#endif
