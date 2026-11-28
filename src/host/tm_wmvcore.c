/*
 * tm_wmvcore.c - wmvcore.dll (Windows Media Format) as a host DLL
 * (docs/IMPORTS.md, "Host DLLs" and "Windows Media and DirectShow").
 *
 * What the game needs from it at start-up is that the file exists in the
 * system directory (part 1 of its Windows Media check, sub_0093C640; part 2
 * is a DirectShow probe, not this DLL). So the DLL is a visible, loadable
 * host DLL of the system directory, and nothing more:
 *
 *   WMCreateProfileManager  film export (compatibility task C): not
 *                           implemented
 *   WMCreateSyncReader      the audio engine's WMA reader (task D): not
 *                           implemented
 *
 * Both exports are now present as explicit failure stubs: GetProcAddress
 * succeeds, but each call returns E_NOTIMPL and clears its output pointer.
 * No COM object, profile or decoder is made here.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "win32hle.h"
#include "tm_loader.h"
#include "tm_wmvcore.h"

/* These entry points fail as HRESULTs rather than triggering delay-load
 * exceptions. This does not provide a WMA reader or profile manager; it lets
 * the game detect the unsupported feature through the documented API. */
static void wmv_WMCreateProfileManager(void) {            /* (IWMProfileManager **) */
    uint32_t out = A32(0);
    if (!out) RET(0x80004003u, 1);                       /* E_POINTER */
    MEM32(out) = 0;
    fprintf(stderr, "[wmvcore] WMCreateProfileManager: profile manager unavailable\n");
    RET(0x80004001u, 1);                                 /* E_NOTIMPL */
}
static void wmv_WMCreateSyncReader(void) {                /* (cert, rights, IWMSyncReader **) */
    uint32_t out = A32(2);
    if (!out) RET(0x80004003u, 3);                       /* E_POINTER */
    MEM32(out) = 0;
    fprintf(stderr, "[wmvcore] WMCreateSyncReader: synchronous ASF reader unavailable\n");
    RET(0x80004001u, 3);                                 /* E_NOTIMPL */
}

static const win32hle_shim g_shims[] = {
    { "wmvcore.dll!WMCreateProfileManager", wmv_WMCreateProfileManager },
    { "wmvcore.dll!WMCreateSyncReader", wmv_WMCreateSyncReader },
    { NULL, NULL }
};
static const tm_host_export g_exports[] = {
    { "WMCreateProfileManager", "wmvcore.dll!WMCreateProfileManager", "returns E_NOTIMPL; film export is unavailable" },
    { "WMCreateSyncReader",     "wmvcore.dll!WMCreateSyncReader", "returns E_NOTIMPL; WMA music playback is unavailable" },
    { 0, 0, 0 }
};
static const tm_host_dll g_dll = {
    .name = "wmvcore.dll", .dir = TM_SYSTEM_DIR, .visible = 1, .loadable = 1, .exports = g_exports,
};

void tm_wmvcore_init(void) {
    win32hle_register(g_shims);
    tm_loader_add(&g_dll);
}
