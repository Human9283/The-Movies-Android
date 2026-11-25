/*
 * tm_itrace.c - the import trace (docs/IMPORTS.md): for every import the game
 * actually calls through its IAT, the number of calls and, for the first
 * TM_ITRACE_DETAIL calls, the first six stack arguments, the return value
 * (eax), the bytes the callee removed from the stack, GetLastError before
 * and after, the calling lifted function and the host thread.
 *
 * Each bound import gets a trampoline shim ("itrace#N") that records the call
 * and runs the real shim through win32hle's own import lookup, so win32hle
 * itself is unchanged. Imports the game fetches with GetProcAddress are not
 * traced here (they do not go through the IAT).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "win32hle.h"
#include "tm_itrace.h"

#define TM_ITRACE_MAX    512
#define TM_ITRACE_DETAIL 4

typedef struct {
    uint32_t args[6], ret, purge, err_before, err_after, caller;
    int      thread, returned;
} call_rec;

static struct {
    const char *name;
    uint32_t    real_va;
    int         status;
    uint64_t    calls;
    int         nd;
    call_rec    d[TM_ITRACE_DETAIL];
} g_it[TM_ITRACE_MAX];
static int g_nit;
static int g_enabled;

static void itrace_call(int i) {
    uint32_t esp0 = g_esp;
    call_rec *r = NULL;
    g_it[i].calls++;
    if (g_it[i].nd < TM_ITRACE_DETAIL) {
        r = &g_it[i].d[g_it[i].nd++];
        for (int k = 0; k < 6; k++) r->args[k] = MEM32(esp0 + 4u + 4u * (uint32_t)k);
        r->caller = g_cur_func;
        r->thread = (int)syscall(SYS_gettid);
        r->err_before = hle_get_last_error();
    }
    recomp_func_t f = recomp_lookup_import(g_it[i].real_va);
    f();
    if (r) {
        r->ret = g_eax;
        r->purge = g_esp - esp0;
        r->err_after = hle_get_last_error();
        r->returned = 1;
    }
}

/* 512 trampolines, one per traced import. Each is named by three octal
 * digits (tr_000 .. tr_777) and passes 0<digits>, an octal literal, so
 * tr_100 passes 0100 = 64: its position in g_tramp. g_index lists the same
 * literals in the same order, and tm_itrace_selfcheck checks every one. */
#define T1(n)   static void tr_##n(void) { itrace_call(0##n); }
#define T8(n)   T1(n##0) T1(n##1) T1(n##2) T1(n##3) T1(n##4) T1(n##5) T1(n##6) T1(n##7)
#define T64(n)  T8(n##0) T8(n##1) T8(n##2) T8(n##3) T8(n##4) T8(n##5) T8(n##6) T8(n##7)
T64(0) T64(1) T64(2) T64(3) T64(4) T64(5) T64(6) T64(7)
#define R1(n)   tr_##n,
#define R8(n)   R1(n##0) R1(n##1) R1(n##2) R1(n##3) R1(n##4) R1(n##5) R1(n##6) R1(n##7)
#define R64(n)  R8(n##0) R8(n##1) R8(n##2) R8(n##3) R8(n##4) R8(n##5) R8(n##6) R8(n##7)
static recomp_func_t g_tramp[TM_ITRACE_MAX] = { R64(0) R64(1) R64(2) R64(3) R64(4) R64(5) R64(6) R64(7) };
#define I1(n)   0##n,
#define I8(n)   I1(n##0) I1(n##1) I1(n##2) I1(n##3) I1(n##4) I1(n##5) I1(n##6) I1(n##7)
#define I64(n)  I8(n##0) I8(n##1) I8(n##2) I8(n##3) I8(n##4) I8(n##5) I8(n##6) I8(n##7)
static const int g_index[TM_ITRACE_MAX] = { I64(0) I64(1) I64(2) I64(3) I64(4) I64(5) I64(6) I64(7) };

static win32hle_shim g_reg[TM_ITRACE_MAX][2];
static char g_regname[TM_ITRACE_MAX][16];

void tm_itrace_enable(void) { g_enabled = 1; }
int  tm_itrace_enabled(void) { return g_enabled; }

uint32_t tm_itrace_wrap(const char *name, uint32_t real_va, int status) {
    if (!g_enabled || g_nit >= TM_ITRACE_MAX) return real_va;
    int i = g_nit++;
    g_it[i].name = name;
    g_it[i].real_va = real_va;
    g_it[i].status = status;
    snprintf(g_regname[i], sizeof g_regname[i], "itrace#%d", i);
    g_reg[i][0].name = g_regname[i];
    g_reg[i][0].fn = g_tramp[i];
    g_reg[i][1].name = NULL;
    win32hle_register(g_reg[i]);
    return hle_resolve(g_regname[i]);
}

/* Every trampoline passes its own position (see g_index). */
int tm_itrace_selfcheck(void) {
    for (int i = 0; i < TM_ITRACE_MAX; i++)
        if (g_index[i] != i) return 0;
    return 1;
}

void tm_itrace_write(const char *path, const char *(*status_name)(int)) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[itrace] cannot write %s\n", path); return; }
    int called = 0;
    for (int i = 0; i < g_nit; i++) called += g_it[i].calls > 0;
    fprintf(f, "{\n \"bound\": %d,\n \"called\": %d,\n \"imports\": [\n", g_nit, called);
    int first = 1;
    for (int i = 0; i < g_nit; i++) {
        if (!g_it[i].calls) continue;
        fprintf(f, "%s  {\"name\": \"%s\", \"status\": \"%s\", \"calls\": %llu, \"first_calls\": [",
                first ? "" : ",\n", g_it[i].name, status_name(g_it[i].status), (unsigned long long)g_it[i].calls);
        first = 0;
        for (int k = 0; k < g_it[i].nd; k++) {
            call_rec *r = &g_it[i].d[k];
            fprintf(f, "%s\n    {\"caller\": \"0x%08X\", \"thread\": %d, \"args\": [", k ? "," : "", r->caller, r->thread);
            for (int a = 0; a < 6; a++) fprintf(f, "%s\"0x%08X\"", a ? ", " : "", r->args[a]);
            if (r->returned)
                fprintf(f, "], \"ret\": \"0x%08X\", \"stack_popped\": %u, \"last_error_before\": %u, \"last_error_after\": %u}",
                        r->ret, r->purge, r->err_before, r->err_after);
            else
                fprintf(f, "], \"returned\": false, \"last_error_before\": %u}", r->err_before);
        }
        fprintf(f, "]}");
    }
    fprintf(f, "\n ]\n}\n");
    fclose(f);
}
