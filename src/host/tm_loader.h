#ifndef TM_LOADER_H
#define TM_LOADER_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "win32hle.h"

/* The guest's Windows directories (the runtime system root, tm_sysroot.c). */
#define TM_WINDOWS_DIR  "C:\\WINDOWS"
#define TM_SYSTEM_DIR   "C:\\WINDOWS\\SYSTEM32"
#define TM_SYSTEM16_DIR "C:\\WINDOWS\\SYSTEM"

/* One export of a host DLL. Implemented when shim names a registered shim;
 * a NULL shim is an export the real DLL has and this host does not implement
 * (why says which compatibility task it belongs to). GetProcAddress gives no
 * address for it. */
typedef struct {
    const char *name;
    const char *shim;
    const char *why;
} tm_host_export;

/* A system DLL this host implements itself (docs/IMPORTS.md, "Host DLLs"). */
typedef struct {
    const char           *name;      /* file name, lower case, with extension: "dinput8.dll" */
    const char           *dir;       /* guest directory it is in; NULL: TM_SYSTEM_DIR */
    int                   visible;   /* listed there by FindFirstFileA / GetFileAttributesA (no file on disk) */
    int                   loadable;  /* LoadLibraryA gives a module (else ERROR_BAD_EXE_FORMAT) */
    const tm_host_export *exports;   /* NULL-terminated */
    int  (*on_load)(void);           /* first reference (process attach); 0 fails the load */
    void (*on_unload)(void);         /* last FreeLibrary (process detach) */
} tm_host_dll;

#define TM_LOADER_MAX_DLLS 16    /* host DLLs that can be registered */

/* LoadLibraryA, GetModuleHandleA, GetProcAddress, FreeLibrary,
 * GetModuleFileNameA: overrides of win32hle's ("tm:<name>"). */
extern const win32hle_shim tm_loader_overrides[];

/* Register a host DLL: 0 (and a message) when the table is full or the
 * name is already registered. */
int  tm_loader_add(const tm_host_dll *dll);
void tm_loader_set_app_dir(const char *guest_exe_path);
/* Reserve the host DLL and win32hle system-module handle ranges in guest
 * memory (inaccessible). tm_loader_add does it on the first call; a host
 * calls it at start-up, before any guest allocation. */
void tm_loader_reserve(void);
/* Whether h is a module in win32hle's table (not a host DLL; NULL is 0). */
int  tm_loader_win32hle_module(uint32_t h);

/* The registered host DLLs, for the file system's virtual entries. */
int                tm_loader_count(void);
const tm_host_dll *tm_loader_dll(int i);
const char        *tm_loader_dir(const tm_host_dll *dll);      /* its guest directory */
/* The visible host DLL named name in the guest directory dir (absolute,
 * normalized, no trailing separator), any case; NULL if none. */
const tm_host_dll *tm_loader_visible(const char *dir, const char *name);

typedef enum {
    TM_LOAD_NOT_FOUND,   /* empty name */
    TM_LOAD_HOST_DLL,    /* a host DLL: already loaded, or found in its directory */
    TM_LOAD_FILE,        /* a DLL file (guest path in guest_out) */
    TM_LOAD_WIN32HLE     /* left to win32hle: its system DLLs, the current directory */
} tm_load_where;
/* Where LoadLibraryA(name) is answered from (the search order in tm_loader.c). */
tm_load_where tm_loader_locate(const char *name, char *guest_out, size_t n);
/* A host DLL's reference count (0: not loaded), -1 if it is not a host DLL. */
int tm_loader_refs(const char *name);

typedef enum {
    TM_EXPORT_NONE,          /* not an export of this DLL (or not a host DLL) */
    TM_EXPORT_UNSUPPORTED,   /* an export of the real DLL, not implemented here */
    TM_EXPORT_IMPLEMENTED
} tm_export_state;
tm_export_state tm_loader_export(const char *dll, const char *export_name);

/* Host-DLL loads and the exports asked for, at exit. */
void tm_loader_report(FILE *out);

#endif
