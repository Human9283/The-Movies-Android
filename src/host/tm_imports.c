/*
 * tm_imports.c - the status of every shim in src/host, and of every win32hle
 * import verified against this game (see tm_imports.h). A shim added anywhere
 * in src/host is added here too: tests/m2_winmain.py fails otherwise. A
 * win32hle import is listed as verified only with its entry in
 * docs/IMPORTS.md and a test.
 */
#include <string.h>
#include "tm_imports.h"

const tm_import_info tm_import_table[] = {
    /* ---- our own implementations ---- */
    { "ConvertSymbols", TM_IMPLEMENTED,
      "helper.dll: CP1251 -> UTF-16 for 0xC0-0xFF, from the DLL's disassembly" },
    { "SHGetFolderPathW", TM_IMPLEMENTED,
      "CSIDL_PERSONAL, APPDATA, LOCAL_APPDATA, COMMON_APPDATA, COMMON_DOCUMENTS, MYPICTURES, MYVIDEO under D:; "
      "any other CSIDL fails with E_FAIL" },
    { "SHGetSpecialFolderPathA", TM_IMPLEMENTED,
      "the same folders; any other CSIDL returns FALSE" },
    { "GetDiskFreeSpaceExW", TM_IMPLEMENTED,
      "statvfs of the resolved directory; NULL = current drive root; errors 2/3/267 as Wine maps them; "
      "tests/c/test_disk_free_w.c" },
    { "GetTempPathA", TM_IMPLEMENTED,
      "D:\\Local Settings\\Temp\\ (the per-user temp of Windows XP under the user drive), never the game directory; "
      "length / size-needed contract; tests/c/test_loader.c" },
    { "LoadLibraryA", TM_IMPLEMENTED,
      "XP SP2 SafeDllSearchMode order (loaded, application, system, 16-bit system, Windows dir; then win32hle) and "
      "host DLLs (dinput8.dll, wmvcore.dll) with reference counts, attach/detach, loadable flag; other modules "
      "delegated to win32hle (files are mapped, their code is not run); tests/c/test_loader.c, test_vfs.c" },
    { "GetModuleHandleA", TM_IMPLEMENTED,
      "host DLLs only while loaded, no reference taken; others delegated to win32hle" },
    { "GetProcAddress", TM_IMPLEMENTED,
      "host DLL exports by name (no ordinals); an export the host does not implement fails with "
      "ERROR_PROC_NOT_FOUND and is reported; others delegated to win32hle; tests/c/test_vfs.c" },
    { "FreeLibrary", TM_IMPLEMENTED,
      "host DLLs: reference count, unloaded at zero, a stale handle fails with ERROR_MOD_NOT_FOUND; others delegated" },
    { "GetModuleFileNameA", TM_IMPLEMENTED,
      "host DLLs: C:\\WINDOWS\\SYSTEM32\\<name>, truncation as Vista+; others delegated to win32hle" },
    { "GetWindowsDirectoryA", TM_IMPLEMENTED,
      "C:\\WINDOWS, the runtime system directory mounted there (tm_sysroot.c); length / size-needed contract; "
      "tests/c/test_sysroot.c" },
    { "GetSystemDirectoryA", TM_IMPLEMENTED,
      "C:\\WINDOWS\\SYSTEM32 in the runtime system directory; length / size-needed contract; tests/c/test_sysroot.c" },
    { "FindFirstFileA", TM_IMPLEMENTED,
      "own listing: host entries merged with mounts (WINDOWS in C:\\) and visible host DLLs, no duplicates, NTFS "
      "order, no . / .. in a root, DOS wildcard rules (? * . at the end), ERROR_PATH_NOT_FOUND / FILE_NOT_FOUND / "
      "FILENAME_EXCED_RANGE / NOT_ENOUGH_MEMORY / TOO_MANY_OPEN_FILES; tests/c/test_vfs.c, test_vfs_audit.c" },
    { "FindNextFileA", TM_IMPLEMENTED,
      "the listing made by FindFirstFileA; ERROR_NO_MORE_FILES (handle stays valid), ERROR_INVALID_HANDLE, "
      "ERROR_INVALID_PARAMETER; tests/c/test_vfs.c, test_vfs_audit.c" },
    { "CoInitialize", TM_IMPLEMENTED,
      "CoInitializeEx(NULL, COINIT_APARTMENTTHREADED): per thread, S_OK then S_FALSE; tests/c/test_ole32.c" },
    { "CoInitializeEx", TM_IMPLEMENTED,
      "per thread: S_OK, S_FALSE for the same model, RPC_E_CHANGED_MODE for the other, E_INVALIDARG for a "
      "reserved pointer; tests/c/test_ole32.c" },
    { "CoUninitialize", TM_IMPLEMENTED, "undoes one counted initialisation of the thread; tests/c/test_ole32.c" },
    { "FindFirstFileW", TM_IMPLEMENTED,
      "FindFirstFileA's search: the pattern converted with the guest's WideCharToMultiByte (CP_ACP), the names back "
      "with MultiByteToWideChar; a pattern outside the ANSI code page stops the run; tests/c/test_vfs.c" },
    { "FindNextFileW", TM_IMPLEMENTED,
      "FindNextFileA on the same handle, the name converted to UTF-16; tests/c/test_vfs.c" },
    { "GetFullPathNameW", TM_IMPLEMENTED,
      "GetFullPathNameA's rules (Windows path normalisation), the name converted both ways (CP_ACP); the file part "
      "points into the wide buffer; a UNC name stops the run; tests/c/test_vfs.c" },
    { "CreateFileW", TM_IMPLEMENTED,
      "CreateFileA's override with the name converted (CP_ACP, as FindFirstFileW); tests/c/test_vfs.c" },
    { "GetFileAttributesW", TM_IMPLEMENTED,
      "GetFileAttributesA's override with the name converted; tests/c/test_vfs.c" },
    { "DeleteFileW", TM_IMPLEMENTED, "DeleteFileA's override with the name converted; tests/c/test_vfs.c" },
    { "CreateDirectoryW", TM_IMPLEMENTED, "CreateDirectoryA's override with the name converted; tests/c/test_vfs.c" },
    { "FindClose", TM_IMPLEMENTED,
      "closes a search handle only; any other handle: FALSE, ERROR_INVALID_HANDLE, left open; "
      "tests/c/test_vfs_audit.c" },
    { "GetFullPathNameA", TM_IMPLEMENTED,
      "lexical, the Windows way: relative to the guest's current directory, C:name, \\name, . and .., trailing dots "
      "and spaces; size-needed contract and file part; UNC names delegated; tests/c/test_vfs.c" },
    { "GetFileAttributesA", TM_IMPLEMENTED,
      "a visible host DLL: FILE_ATTRIBUTE_ARCHIVE; a mount: its host directory; ERROR_FILE_NOT_FOUND / "
      "PATH_NOT_FOUND by whether the parent exists, FILENAME_EXCED_RANGE; UNC delegated; "
      "tests/c/test_vfs.c, test_vfs_audit.c" },
    { "CreateFileA", TM_IMPLEMENTED,
      "win32hle's, except a host DLL entry (no contents): ERROR_ACCESS_DENIED; tests/c/test_vfs.c" },
    { "DeleteFileA", TM_IMPLEMENTED,
      "win32hle's, except a host DLL entry: ERROR_ACCESS_DENIED; tests/c/test_vfs.c" },
    { "CreateDirectoryA", TM_IMPLEMENTED,
      "win32hle's, except the name of a host DLL entry: ERROR_ALREADY_EXISTS; tests/c/test_vfs.c" },
    /* ---- diagnostic ---- */
    { "RaiseException", TM_DIAGNOSTIC,
      "overrides win32hle's: decodes the exception (delay-load DelayLoadInfo) and stops the run, "
      "as win32hle does; guest exceptions are not dispatched (B4)" },
    { "MessageBoxW", TM_DIAGNOSTIC,
      "no display: text and caption printed, answered No if the box has one, else its first button" },
    /* ---- win32hle, verified against the game's calls ---- */
    /* (none yet: docs/IMPORTS.md) */
    { NULL, 0, NULL }
};

const char *tm_import_status_name(tm_import_status s) {
    switch (s) {
    case TM_IMPLEMENTED:       return "implemented";
    case TM_DIAGNOSTIC:        return "diagnostic";
    case TM_WIN32HLE_PRESENT:  return "win32hle-present";
    case TM_WIN32HLE_VERIFIED: return "win32hle-verified";
    default:                   return "unsupported";
    }
}

int tm_import_listed_status(const char *name, const char **note) {
    for (const tm_import_info *i = tm_import_table; i->name; i++)
        if (!strcmp(i->name, name)) { if (note) *note = i->note; return (int)i->status; }
    return -1;
}
