/*
 * tm_except.c - RaiseException, decoded before the run stops.
 *
 * win32hle does not dispatch guest exceptions (no SEH, no C++ EH: docs/AUDIT.md
 * B4); its RaiseException prints the code and exits. This shim does the same,
 * and first decodes what the exception carries, so a stop names its cause
 * from the game's own data rather than from the code alone.
 *
 * The MSVC delay-load helper (__delayLoadHelper2) raises
 *   VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND)  = 0xC06D007E
 *   VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND) = 0xC06D007F
 * with one argument, a pointer to its DelayLoadInfo (delayimp.h):
 *   +0x00 DWORD  cb              size of the structure (0x24)
 *   +0x04 PCImgDelayDescr pidd   the delay import descriptor
 *   +0x08 FARPROC *ppfn          the IAT slot being bound
 *   +0x0C LPCSTR szDll           the DLL name, as in the descriptor
 *   +0x10 DelayLoadProc dlp      { BOOL fImportByName; LPCSTR szProcName | DWORD dwOrdinal }
 *   +0x18 HMODULE hmodCur        0 if LoadLibrary failed (dliFailLoadLib)
 *   +0x1C FARPROC pfnCur         0 if GetProcAddress failed (dliFailGetProc)
 *   +0x20 DWORD  dwLastError     GetLastError() right after the failing call
 * ImgDelayDescr (VC7+, grAttrs bit 0 = RVAs): grAttrs, rvaDLLName, rvaHmod,
 * rvaIAT, rvaINT, rvaBoundIAT, rvaUnloadIAT, dwTimeStamp.
 *
 * The failing stage is read from hmodCur / pfnCur, not from the code: a
 * ..7E with a module handle, or a ..7F without one, is reported as such.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "tm_except.h"

#define IMAGE_BASE 0x00400000u

static uint32_t g_raise_count;
uint32_t tm_raise_count(void) { return g_raise_count; }

/* A guest C string, bounded and printable, for the log. */
static const char *gstr(uint32_t va, char *buf, size_t n) {
    if (!va) return "(null)";
    size_t k = 0;
    for (; k + 1 < n; k++) {
        uint8_t c = MEM8(va + (uint32_t)k);
        if (!c) break;
        buf[k] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    buf[k] = 0;
    return buf;
}

static void decode_delay_load(uint32_t code, uint32_t pdli) {
    char a[128], b[128];
    uint32_t cb = MEM32(pdli), pidd = MEM32(pdli + 4), ppfn = MEM32(pdli + 8), szdll = MEM32(pdli + 12);
    uint32_t by_name = MEM32(pdli + 16), proc = MEM32(pdli + 20);
    uint32_t hmod = MEM32(pdli + 24), pfn = MEM32(pdli + 28), err = MEM32(pdli + 32);
    fprintf(stderr, "[except] delay-load failure, DelayLoadInfo at 0x%08X (cb 0x%X)\n", pdli, cb);
    fprintf(stderr, "[except]   dll \"%s\", import %s%s%s", gstr(szdll, a, sizeof a),
            by_name ? "\"" : "#", by_name ? gstr(proc, b, sizeof b) : "", by_name ? "\"" : "");
    if (!by_name) fprintf(stderr, "%u", proc);
    fprintf(stderr, ", IAT slot 0x%08X\n", ppfn);
    const char *stage = !hmod ? "dliFailLoadLib (LoadLibrary returned NULL)"
                      : !pfn  ? "dliFailGetProc (GetProcAddress returned NULL)"
                              : "neither: module and procedure both present";
    fprintf(stderr, "[except]   hmodCur 0x%08X, pfnCur 0x%08X: %s; dwLastError %u\n", hmod, pfn, stage, err);
    if ((code == 0xC06D007Eu && hmod) || (code == 0xC06D007Fu && !hmod))
        fprintf(stderr, "[except]   note: the code (0x%08X) and the recorded stage disagree\n", code);
    if (pidd) {
        uint32_t attrs = MEM32(pidd), name = MEM32(pidd + 4), rhmod = MEM32(pidd + 8), iat = MEM32(pidd + 12);
        uint32_t intab = MEM32(pidd + 16), ts = MEM32(pidd + 28);
        uint32_t base = (attrs & 1u) ? IMAGE_BASE : 0;   /* bit 0: the fields are RVAs */
        fprintf(stderr, "[except]   descriptor 0x%08X: grAttrs 0x%X, name \"%s\", hmod slot 0x%08X (= 0x%08X), "
                "IAT 0x%08X, INT 0x%08X, timestamp 0x%08X\n", pidd, attrs, gstr(base + name, a, sizeof a),
                base + rhmod, MEM32(base + rhmod), base + iat, base + intab, ts);
    }
}

static void k_RaiseException(void) {                    /* (code, flags, nargs, args) */
    uint32_t code = A32(0), flags = A32(1), nargs = A32(2), args = A32(3);
    g_raise_count++;
    fprintf(stderr, "[except] RaiseException(0x%08X, flags 0x%X, %u argument%s) from guest 0x%08X\n",
            code, flags, nargs, nargs == 1 ? "" : "s", g_cur_func);
    for (uint32_t i = 0; i < nargs && i < 15 && args; i++)
        fprintf(stderr, "[except]   ExceptionInformation[%u] = 0x%08X\n", i, MEM32(args + 4 * i));
    if ((code == 0xC06D007Eu || code == 0xC06D007Fu) && nargs >= 1 && args && MEM32(args))
        decode_delay_load(code, MEM32(args));
    fprintf(stderr, "[win32hle] RaiseException(0x%08X) from guest 0x%08X: guest exceptions are not handled\n",
            code, g_cur_func);
    hle_exit(3);
}

/* Registered under "tm:<name>": tm_host's resolver takes these before
 * win32hle's own shim of the same name. */
const win32hle_shim tm_except_overrides[] = {
    { "tm:RaiseException", k_RaiseException },
    { 0, 0 }
};
