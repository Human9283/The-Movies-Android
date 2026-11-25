#ifndef TM_VFS_H
#define TM_VFS_H
#include <stddef.h>
#include "win32hle.h"

/* Serve the guest directory guest (absolute, below a drive's root) from the
 * host directory host. Refused (0, why) when it overlaps another mount, on
 * either side. */
int tm_vfs_mount(const char *guest, const char *host, char *why, size_t n);
/* 1 if a guest path names a virtual file entry (a visible host DLL). */
int tm_vfs_virtual_file(const char *guest);
/* Windows wildcard match without case. */
int tm_vfs_wild(const char *mask, const char *name);

/* FindFirstFileA, FindNextFileA, GetFileAttributesA, GetFullPathNameA,
 * CreateFileA, DeleteFileA, CreateDirectoryA ("tm:<name>"). */
extern const win32hle_shim tm_vfs_overrides[];
#endif
