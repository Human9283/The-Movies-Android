#ifndef TM_WINMM_H
#define TM_WINMM_H
#include "win32hle.h"
/* winmm.dll functions win32hle does not answer (tm_winmm.c): waveInGetDevCapsA */
extern const win32hle_shim tm_winmm_overrides[];
#endif
