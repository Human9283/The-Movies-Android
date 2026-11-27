/*
 * tm_shell.c - shell folder lookups for the guest: SHGetFolderPathW
 * (shfolder.dll) and SHGetSpecialFolderPathA (shell32.dll).
 *
 * Movies.exe 1.1 asks for CSIDL_PERSONAL ("My Documents") from a C++ global
 * constructor before WinMain (docs/M2.md). Every folder lives under the guest
 * drive D:, which the host maps to a writable per-user directory
 * (tm_shell_init), so saves and settings never land in the game directory.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "win32hle.h"
#include "tm_shell.h"

#define S_OK          0x00000000u
#define E_INVALIDARG  0x80070057u
#define E_FAIL        0x80004005u
#define CSIDL_FLAG_CREATE 0x8000u
#define GUEST_MAX_PATH 260u

static char g_user_root[4096];

/* CSIDL -> folder under D:\ (NULL: not one we answer) */
static const char *csidl_folder(uint32_t csidl) {
    switch (csidl & 0xFFu) {
    case 0x05: return "My Documents";                 /* CSIDL_PERSONAL */
    case 0x1A: return "Application Data";             /* CSIDL_APPDATA */
    case 0x1C: return "Local Settings\\Application Data"; /* CSIDL_LOCAL_APPDATA */
    case 0x23: return "All Users\\Application Data";  /* CSIDL_COMMON_APPDATA */
    case 0x2E: return "All Users\\Documents";         /* CSIDL_COMMON_DOCUMENTS */
    case 0x27: return "My Documents\\My Pictures";    /* CSIDL_MYPICTURES */
    case 0x0E: return "My Documents\\My Videos";      /* CSIDL_MYVIDEO */
    default:   return NULL;
    }
}

static void mkdirs(const char *path) {
    char buf[4096];
    snprintf(buf, sizeof buf, "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(buf, 0755); *p = '/'; }
    mkdir(buf, 0755);
}

/* Fill `guest` with D:\<folder>; create the host directory if asked (or
 * always: a game that asks for My Documents writes there next). */
static int folder_path(uint32_t csidl, char *guest, size_t n) {
    const char *f = csidl_folder(csidl);
    if (!f || !g_user_root[0]) return 0;
    snprintf(guest, n, "D:\\%s", f);
    char host[4096];
    snprintf(host, sizeof host, "%s/%s", g_user_root, f);
    for (char *p = host; *p; p++) if (*p == '\\') *p = '/';
    mkdirs(host);
    return 1;
}

/* HRESULT SHGetFolderPathW(HWND, int csidl, HANDLE token, DWORD flags, LPWSTR path) */
static void s_SHGetFolderPathW(void) {
    uint32_t csidl = A32(1), out = A32(4);
    char guest[GUEST_MAX_PATH];
    if (!out) RET(E_INVALIDARG, 5);
    if (!folder_path(csidl, guest, sizeof guest)) {
        fprintf(stderr, "[shell] SHGetFolderPathW(csidl 0x%X): not answered\n", csidl);
        MEM16(out) = 0;
        RET(E_FAIL, 5);
    }
    uint32_t i = 0;
    for (; guest[i] && i < GUEST_MAX_PATH - 1; i++) MEM16(out + 2u * i) = (uint8_t)guest[i];
    MEM16(out + 2u * i) = 0;
    fprintf(stderr, "[shell] SHGetFolderPathW(csidl 0x%X) -> %s\n", csidl, guest);
    RET(S_OK, 5);
}

/* BOOL SHGetSpecialFolderPathA(HWND, LPSTR path, int csidl, BOOL create) */
static void s_SHGetSpecialFolderPathA(void) {
    uint32_t out = A32(1), csidl = A32(2);
    char guest[GUEST_MAX_PATH];
    if (!out || !folder_path(csidl, guest, sizeof guest)) {
        fprintf(stderr, "[shell] SHGetSpecialFolderPathA(csidl 0x%X): not answered\n", csidl);
        if (out) MEM8(out) = 0;
        RET(0, 4);
    }
    uint32_t i = 0;
    for (; guest[i] && i < GUEST_MAX_PATH - 1; i++) MEM8(out + i) = (uint8_t)guest[i];
    MEM8(out + i) = 0;
    fprintf(stderr, "[shell] SHGetSpecialFolderPathA(csidl 0x%X) -> %s\n", csidl, guest);
    RET(1, 4);
}

const win32hle_shim tm_shell_shims[] = {
    { "SHGetFolderPathW",        s_SHGetFolderPathW },
    { "SHGetSpecialFolderPathA", s_SHGetSpecialFolderPathA },
    { NULL, NULL }
};

void tm_shell_init(const char *user_root) {
    snprintf(g_user_root, sizeof g_user_root, "%s", user_root);
    mkdirs(g_user_root);
    hle_set_drive('d', g_user_root);
}
