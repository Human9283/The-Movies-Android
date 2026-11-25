#ifndef TM_EXCEPT_H
#define TM_EXCEPT_H
#include <stdint.h>
#include "win32hle.h"

/* RaiseException, decoding delay-load failures before the run stops
 * (tm_except.c). Registered as "tm:RaiseException": an override. */
extern const win32hle_shim tm_except_overrides[];
uint32_t tm_raise_count(void);

#endif
