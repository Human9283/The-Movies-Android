/*
 * tm_sysroot.c - the guest's Windows directory (docs/IMPORTS.md, "The
 * Windows directory").
 *
 * Three host directories, kept apart:
 *   the game     guest C:\ (the install: read; never written by a run)
 *   the system   guest C:\WINDOWS, with C:\WINDOWS\SYSTEM32 in it: a
 *                runtime directory of its own, mounted over that part of C:
 *                (tm_vfs_mount, patches/pcrecomp/0001)
 *   the user     guest D:\ (profile folders, saves, temporary files)
 * None may be inside another: tm_sysroot_init refuses that.
 *
 * The system directory starts empty. Nothing is put in it to satisfy a check:
 * a DLL the host implements is answered by the loader (tm_loader.c), and is
 * listed in C:\WINDOWS\SYSTEM32 as a virtual entry (tm_vfs.c), not a file.
 *
 * GetWindowsDirectoryA and GetSystemDirectoryA give C:\WINDOWS and
 * C:\WINDOWS\SYSTEM32 with the documented contract: the length without the
 * terminator when it fits in uSize, else the size needed including it.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "win32hle.h"
#include "tm_loader.h"
#include "tm_sysroot.h"
#include "tm_vfs.h"

static int inside(const char *a, const char *b) {       /* a at or under b */
    size_t l = strlen(b);
    return !strncmp(a, b, l) && (a[l] == '/' || !a[l]);
}

int tm_sysroot_check_apart(const char *game, const char *system, const char *user, char *why, size_t n) {
    char g[PATH_MAX], s[PATH_MAX], u[PATH_MAX];
    if (!realpath(game, g) || !realpath(system, s) || !realpath(user, u)) {
        snprintf(why, n, "a directory does not exist");
        return 0;
    }
    const char *name[3] = { "game", "system", "user" }, *p[3] = { g, s, u };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (i != j && inside(p[i], p[j])) {
                snprintf(why, n, "the %s directory %s is inside the %s directory %s", name[i], p[i], name[j], p[j]);
                return 0;
            }
    return 1;
}

int tm_sysroot_init(const char *game, const char *system, const char *user) {
    char sys32[PATH_MAX];
    if (mkdir(system, 0755) && errno != EEXIST) { fprintf(stderr, "[sysroot] cannot create %s\n", system); return 0; }
    snprintf(sys32, sizeof sys32, "%s/SYSTEM32", system);
    if (mkdir(sys32, 0755) && errno != EEXIST) { fprintf(stderr, "[sysroot] cannot create %s\n", sys32); return 0; }
    char why[3 * PATH_MAX];
    if (!tm_sysroot_check_apart(game, system, user, why, sizeof why)) {
        fprintf(stderr, "[sysroot] refusing to run: %s\n", why);
        return 0;
    }
    if (!tm_vfs_mount(TM_WINDOWS_DIR, system, why, sizeof why)) {
        fprintf(stderr, "[sysroot] refusing to run: %s\n", why);
        return 0;
    }
    return 1;
}

static uint32_t copy_dir(const char *path, char *buf, uint32_t size) {
    uint32_t len = (uint32_t)strlen(path);
    if (!buf || size < len + 1) return len + 1;
    memcpy(buf, path, len + 1);
    return len;
}
static void k_GetWindowsDirectoryA(void) { RET(copy_dir(TM_WINDOWS_DIR, ASTR(0), A32(1)), 2); }   /* (buf, size) */
static void k_GetSystemDirectoryA(void)  { RET(copy_dir(TM_SYSTEM_DIR, ASTR(0), A32(1)), 2); }

const win32hle_shim tm_sysroot_overrides[] = {
    { "tm:GetWindowsDirectoryA", k_GetWindowsDirectoryA },
    { "tm:GetSystemDirectoryA",  k_GetSystemDirectoryA },
    { 0, 0 }
};
