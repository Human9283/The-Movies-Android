#ifndef TM_OLE32_H
#define TM_OLE32_H
#include "win32hle.h"
/* COM initialisation per guest thread (tm_ole32.c): CoInitialize,
 * CoInitializeEx, CoUninitialize as overrides of win32hle's. */
extern const win32hle_shim tm_ole32_overrides[];
/* the apartment of the calling guest thread: 0 none, 1 single-threaded, 2 multithreaded */
int tm_ole32_apartment(void);
#endif
