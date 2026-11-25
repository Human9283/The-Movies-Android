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
 * Both are listed as exports of the real DLL with no implementation, so
 * GetProcAddress gives no address for them (ERROR_PROC_NOT_FOUND) and says
 * which task each belongs to. The game reaches them only through its delay-
 * load helper, which then raises 0xC06D007F (dliFailGetProc); tm_except.c
 * decodes and reports it. No COM object, profile or decoder is made here.
 */
#include <stddef.h>
#include "tm_loader.h"
#include "tm_wmvcore.h"

static const tm_host_export g_exports[] = {
    { "WMCreateProfileManager", NULL, "the profile manager (film export) is not implemented" },
    { "WMCreateSyncReader",     NULL, "the synchronous ASF reader (WMA music) is not implemented" },
    { 0, 0, 0 }
};
static const tm_host_dll g_dll = {
    .name = "wmvcore.dll", .dir = TM_SYSTEM_DIR, .visible = 1, .loadable = 1, .exports = g_exports,
};

void tm_wmvcore_init(void) { tm_loader_add(&g_dll); }
