/*
 * tm_kernel32.c - kernel32 functions the game needs that win32hle does not
 * provide. Written against the Windows documentation; where
 * the documentation leaves an error case open, the behaviour follows Wine's
 * implementation (kernelbase/volume.c) and the NT status -> Win32 error
 * mapping. Not run on real Windows: see docs/IMPORTS.md.
 *
 * GetDiskFreeSpaceExW(LPCWSTR dir, PULARGE_INTEGER avail_to_caller,
 *                     PULARGE_INTEGER total, PULARGE_INTEGER total_free)
 *   stdcall, 4 arguments. dir is any directory on the volume; NULL means the
 *   root of the current drive. Each output is optional and receives a 64-bit
 *   little-endian byte count. Returns nonzero on success (GetLastError
 *   untouched), 0 on failure with GetLastError set:
 *     ERROR_PATH_NOT_FOUND (3)  drive not mapped, empty path, or a missing
 *                               directory above the last component
 *     ERROR_FILE_NOT_FOUND (2)  the last component does not exist
 *     ERROR_DIRECTORY    (267)  the path names a file, not a directory
 *     ERROR_INVALID_NAME (123)  a path longer than 32767 UTF-16 units
 *     ERROR_NOT_SUPPORTED (50)  UNC paths (\\server\share\): no network shares here
 *   The caller's available bytes are statvfs f_bavail (the blocks an
 *   unprivileged user may still use); total free is f_bfree.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "win32hle.h"
#include "tm_kernel32.h"

#define ERROR_FILE_NOT_FOUND   2u
#define ERROR_PATH_NOT_FOUND   3u
#define ERROR_NOT_SUPPORTED   50u
#define ERROR_INVALID_PARAMETER 87u
#define ERROR_INVALID_NAME   123u
#define ERROR_DIRECTORY      267u
#define ERROR_ARITHMETIC_OVERFLOW 534u
#define MAX_WPATH 32767u

/* UTF-16 at guest va -> UTF-8 in out. 0 if longer than MAX_WPATH units or out
 * is too small. Unpaired surrogates become U+FFFD. */
static int guest_wide_to_utf8(uint32_t va, char *out, size_t n) {
    size_t k = 0;
    for (uint32_t i = 0;; i++) {
        if (i > MAX_WPATH) return 0;
        uint16_t u = MEM16(va + 2u * i);
        if (!u) break;
        uint32_t c = u;
        if (u >= 0xD800 && u <= 0xDBFF) {
            uint16_t lo = MEM16(va + 2u * (i + 1));
            if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000u + ((u - 0xD800u) << 10) + (lo - 0xDC00u); i++; }
            else c = 0xFFFD;
        } else if (u >= 0xDC00 && u <= 0xDFFF) c = 0xFFFD;
        char b[4]; int m;
        if (c < 0x80) b[0] = (char)c, m = 1;
        else if (c < 0x800) b[0] = (char)(0xC0 | c >> 6), b[1] = (char)(0x80 | (c & 63)), m = 2;
        else if (c < 0x10000) b[0] = (char)(0xE0 | c >> 12), b[1] = (char)(0x80 | (c >> 6 & 63)), b[2] = (char)(0x80 | (c & 63)), m = 3;
        else b[0] = (char)(0xF0 | c >> 18), b[1] = (char)(0x80 | (c >> 12 & 63)), b[2] = (char)(0x80 | (c >> 6 & 63)), b[3] = (char)(0x80 | (c & 63)), m = 4;
        if (k + (size_t)m + 1 > n) return 0;
        memcpy(out + k, b, (size_t)m); k += (size_t)m;
    }
    out[k] = 0;
    return 1;
}

static int mul_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (a && b > UINT64_MAX / a) return 0;
    *out = a * b; return 1;
}

int tm_disk_free_space(const char *guest, uint64_t out[3], uint32_t *err) {
    char root[4], host[1024], cur[1024], gcur[1024];
    if (!guest) {                                   /* the root of the current drive */
        if (!getcwd(cur, sizeof cur)) { *err = ERROR_PATH_NOT_FOUND; return 0; }
        hle_guest_path(cur, gcur, sizeof gcur);
        if (gcur[1] != ':') { *err = ERROR_PATH_NOT_FOUND; return 0; }
        root[0] = gcur[0]; root[1] = ':'; root[2] = '\\'; root[3] = 0;
        guest = root;
    }
    if (!guest[0]) { *err = ERROR_PATH_NOT_FOUND; return 0; }
    if ((guest[0] == '\\' || guest[0] == '/') && (guest[1] == '\\' || guest[1] == '/')) {
        *err = ERROR_NOT_SUPPORTED; return 0;
    }
    if (!hle_host_path_base(guest, host, sizeof host)) {
        /* Which component is missing: the last one (2) or one above it (3). */
        char parent[1024];
        size_t l = strlen(guest);
        if (l >= sizeof parent) { *err = ERROR_PATH_NOT_FOUND; return 0; }
        memcpy(parent, guest, l + 1);
        while (l && (parent[l - 1] == '\\' || parent[l - 1] == '/')) parent[--l] = 0;
        while (l && parent[l - 1] != '\\' && parent[l - 1] != '/' && parent[l - 1] != ':') parent[--l] = 0;
        struct stat ps;
        *err = l && hle_host_path_base(parent, host, sizeof host) && stat(host, &ps) == 0 && S_ISDIR(ps.st_mode)
             ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
        /* a drive that is not mapped has no parent to find */
        if (l <= 3 && parent[1] == ':' && !hle_host_path_base(parent, host, sizeof host)) *err = ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat st;
    if (stat(host, &st) != 0) { *err = errno == ENOTDIR ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND; return 0; }
    if (!S_ISDIR(st.st_mode)) { *err = ERROR_DIRECTORY; return 0; }
    struct statvfs fs;
    if (statvfs(host, &fs) != 0) { *err = ERROR_PATH_NOT_FOUND; return 0; }
    uint64_t unit = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
    if (!unit || !mul_u64(unit, fs.f_bavail, &out[0]) || !mul_u64(unit, fs.f_blocks, &out[1]) ||
        !mul_u64(unit, fs.f_bfree, &out[2])) { *err = ERROR_ARITHMETIC_OVERFLOW; return 0; }
    return 1;
}

static void k_GetDiskFreeSpaceExW(void) {
    uint32_t dir = A32(0), outs[3] = { A32(1), A32(2), A32(3) };
    char guest[4 * MAX_WPATH + 4];
    /* An output that would run past the 4 GB guest space: host-memory guard
     * (Windows would fault writing it). */
    for (int i = 0; i < 3; i++)
        if (outs[i] && outs[i] > UINT32_MAX - 7u) { hle_set_last_error(ERROR_INVALID_PARAMETER); RET(0, 4); }
    if (dir && !guest_wide_to_utf8(dir, guest, sizeof guest)) { hle_set_last_error(ERROR_INVALID_NAME); RET(0, 4); }
    uint64_t v[3];
    uint32_t err = 0;
    int ok = tm_disk_free_space(dir ? guest : NULL, v, &err);
    if (hle_filetrace())
        fprintf(stderr, "[kernel32] GetDiskFreeSpaceExW(%s%s%s) -> %s %u\n", dir ? "\"" : "", dir ? guest : "NULL",
                dir ? "\"" : "", ok ? "ok" : "error", ok ? 0u : err);
    if (!ok) { hle_set_last_error(err); RET(0, 4); }
    for (int i = 0; i < 3; i++) if (outs[i]) memcpy(hle_ptr(outs[i]), &v[i], 8);
    RET(1, 4);
}

/* GetTempPathA(nBufferLength, lpBuffer). Windows returns the first of %TMP%,
 * %TEMP%, %USERPROFILE% and the Windows directory, with a trailing backslash.
 * The guest has no environment for these, so the answer is the per-user
 * temporary directory of Windows XP under the user profile, here the guest's
 * D: (where the shell folders already are): D:\Local Settings\Temp\. It is
 * created if missing. win32hle's own answer, C:\WINDOWS\TEMP\, is inside the
 * game directory (C:), so the game's temporary files would be written there.
 * Return: the length without the terminator if it fits in nBufferLength;
 * otherwise the size needed including the terminator, buffer untouched. */
uint32_t tm_temp_path(char *buf, uint32_t n) {
    static const char path[] = "D:\\Local Settings\\Temp\\";
    uint32_t len = (uint32_t)strlen(path);
    if (!buf || n < len + 1) return len + 1;
    char host[1024];
    hle_host_path_for_write("D:\\Local Settings", host, sizeof host, 0);
    mkdir(host, 0755);
    hle_host_path_for_write("D:\\Local Settings\\Temp", host, sizeof host, 0);
    if (mkdir(host, 0755) == 0) hle_path_changed();
    memcpy(buf, path, len + 1);
    return len;
}
static void k_GetTempPathA(void) {                      /* (nBufferLength, lpBuffer) */
    uint32_t r = tm_temp_path(ASTR(1), A32(0));
    if (hle_filetrace()) fprintf(stderr, "[kernel32] GetTempPathA(%u) -> %u\n", A32(0), r);
    RET(r, 2);
}

const win32hle_shim tm_kernel32_shims[] = {
    { "GetDiskFreeSpaceExW", k_GetDiskFreeSpaceExW },
    { 0, 0 }
};

const win32hle_shim tm_kernel32_overrides[] = {
    { "tm:GetTempPathA", k_GetTempPathA },
    { 0, 0 }
};
