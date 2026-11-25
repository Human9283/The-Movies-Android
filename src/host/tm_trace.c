/*
 * tm_trace.c - a portable stand-in for pcrecomp's runtime/recomp32/recomp_trace.c,
 * which includes <windows.h>. Only what tm_host needs: the RECOMP_ENTER ring
 * buffer, recomp_dump_trace, and the recomp_trace_extra hook. Options
 * (--calltrace, --firsthit, ...) are not implemented here: recomp_trace_arg
 * takes none.
 */
#include <stdint.h>
#include <stdio.h>
#include "recomp_types.h"
#include "recomp_trace.h"

uint32_t g_enter_trace[RECOMP_ENTER_SIZE];
uint32_t g_enter_idx;
void (*recomp_trace_extra)(uint32_t va);
/* g_cur_import is win32hle's (host_lite.c). */

/* Only the thread holding the machine lock runs lifted code (win32hle), so
 * the ring needs no atomics. */
void recomp_trace_enter(uint32_t va) {
    g_enter_trace[g_enter_idx++ & (RECOMP_ENTER_SIZE - 1)] = va;
    if (recomp_trace_extra) recomp_trace_extra(va);
}

void recomp_dump_trace(const char *why) {
    uint32_t n = g_enter_idx < 32u ? g_enter_idx : 32u;
    fprintf(stderr, "[trace] %s: last %u function entries, oldest first\n", why ? why : "", n);
    for (uint32_t i = g_enter_idx - n; i != g_enter_idx; i++)
        fprintf(stderr, "  sub_%08X\n", g_enter_trace[i & (RECOMP_ENTER_SIZE - 1)]);
}

int  recomp_trace_arg(int argc, char **argv, int i) { (void)argc; (void)argv; (void)i; return 0; }
void recomp_trace_flush(void) { fflush(stderr); }
void recomp_trace_help(void) { }
