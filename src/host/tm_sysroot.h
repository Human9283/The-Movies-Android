#ifndef TM_SYSROOT_H
#define TM_SYSROOT_H
#include <stddef.h>
#include "win32hle.h"
#include "tm_loader.h"

/* From patches/pcrecomp/0001-hle_path-mounts.patch (win32hle's hle_path.c):
 * guest paths at or under guest_dir resolve under host_dir; NULL removes it. */
void hle_set_mount(const char *guest_dir, const char *host_dir);

/* 1 if no one of the three directories is at or under another; else 0, why. */
int tm_sysroot_check_apart(const char *game, const char *system, const char *user, char *why, size_t n);
/* Create the system directory (and SYSTEM32), check the three are apart, and
 * mount it at C:\WINDOWS. 0 on failure (printed). */
int tm_sysroot_init(const char *game, const char *system, const char *user);

/* GetWindowsDirectoryA, GetSystemDirectoryA: overrides ("tm:<name>"). */
extern const win32hle_shim tm_sysroot_overrides[];

#endif
