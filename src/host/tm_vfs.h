#ifndef TM_VFS_H
#define TM_VFS_H
#include <stddef.h>
#include <stdint.h>
#include "win32hle.h"

/* Serve the guest directory guest (absolute, below a drive's root) from the
 * host directory host. Refused (0, why) when it overlaps another mount, on
 * either side. */
int tm_vfs_mount(const char *guest, const char *host, char *why, size_t n);
/* 1 if a guest path names a virtual file entry (a visible host DLL). */
int tm_vfs_virtual_file(const char *guest);
/* FindFirstFile's mask matching: name against a mask as the game passes it
 * (rewritten into the DOS wildcard forms when it has a wildcard). */
int tm_vfs_wild(const char *mask, const char *name);
/* The rewritten mask (DOS_STAR = 1, DOS_QM = 2, DOS_DOT = 3); 0 if it does
 * not fit in n. */
int tm_vfs_fixup_mask(const char *mask, char *out, size_t n);
/* GetFileAttributesA's answer for a guest name: the attributes, or
 * 0xFFFFFFFF with *err set to the error Windows gives. */
uint32_t tm_vfs_attributes(const char *name, uint32_t *err);
/* Tests: make the after-th allocation from now on fail (-1: never). */
void tm_vfs_test_fail_alloc(int after);

/* FindFirstFileA, FindNextFileA, FindClose, GetFileAttributesA,
 * GetFullPathNameA, CreateFileA, DeleteFileA, CreateDirectoryA ("tm:<name>"). */
extern const win32hle_shim tm_vfs_overrides[];
#endif
