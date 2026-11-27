#ifndef TM_KERNEL32_H
#define TM_KERNEL32_H
#include <stdint.h>
#include "win32hle.h"

/* GetDiskFreeSpaceExW (see tm_kernel32.c). */
extern const win32hle_shim tm_kernel32_shims[];

/* The volume under guest path (UTF-8; NULL: the current drive's root):
 * out = { available to the caller, total, total free } in bytes. 1 on
 * success, else 0 and *err the Win32 error. */
int tm_disk_free_space(const char *guest, uint64_t out[3], uint32_t *err);

/* GetTempPathA's answer (D:\\Local Settings\\Temp\\): its length if it fits
 * in n, else the size needed with the terminator. */
uint32_t tm_temp_path(char *buf, uint32_t n);
/* GetTempPathA: an override of win32hle's ("tm:GetTempPathA"). */
extern const win32hle_shim tm_kernel32_overrides[];

#endif
