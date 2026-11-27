/*
 * tm_ole32.c - COM initialisation per thread, as the COM documentation for
 * CoInitializeEx and CoUninitialize describes it:
 *   - the first successful call on a thread returns S_OK and sets the
 *     thread's apartment: single-threaded (COINIT_APARTMENTTHREADED) or
 *     multithreaded (otherwise);
 *   - a later call with the same model returns S_FALSE and counts;
 *   - a call with the other model returns RPC_E_CHANGED_MODE and counts
 *     nothing;
 *   - CoUninitialize undoes one counted call; the last one leaves the
 *     apartment.
 * CoInitialize(NULL) is CoInitializeEx(NULL, COINIT_APARTMENTTHREADED). A
 * reserved argument that is not NULL is E_INVALIDARG.
 *
 * win32hle's CoInitialize answers S_OK every time; this replaces it so that
 * a second initialisation on a thread reads S_FALSE, as on Windows. Nothing
 * else here depends on the apartment: COM objects (CoCreateInstance) are
 * created the same way in either.
 *
 * Threads: under win32hle's machine lock, one guest thread at a time.
 */
#include <stdint.h>
#include <stdio.h>
#include "win32hle.h"
#include "tm_ole32.h"

#define S_OK               0x00000000u
#define S_FALSE            0x00000001u
#define E_INVALIDARG       0x80070057u
#define E_OUTOFMEMORY      0x8007000Eu
#define RPC_E_CHANGED_MODE 0x80010106u
#define COINIT_APARTMENTTHREADED 0x2u

#define MAX_THREADS 64
static struct { uint32_t tid; int count, model; } g_thr[MAX_THREADS];

static uint32_t thread_id(void) { return hle_thread_id(); }     /* GetCurrentThreadId's answer */
static int slot(uint32_t tid, int make) {
    for (int i = 0; i < MAX_THREADS; i++) if (g_thr[i].count && g_thr[i].tid == tid) return i;
    if (!make) return -1;
    for (int i = 0; i < MAX_THREADS; i++) if (!g_thr[i].count) { g_thr[i].tid = tid, g_thr[i].model = 0; return i; }
    return -1;
}

static uint32_t init(uint32_t reserved, uint32_t coinit) {
    if (reserved) return E_INVALIDARG;
    int model = coinit & COINIT_APARTMENTTHREADED ? 1 : 2;
    int i = slot(thread_id(), 1);
    if (i < 0) return E_OUTOFMEMORY;
    if (!g_thr[i].count) { g_thr[i].count = 1, g_thr[i].model = model; return S_OK; }
    if (g_thr[i].model != model) return RPC_E_CHANGED_MODE;
    g_thr[i].count++;
    return S_FALSE;
}
static void k_CoInitialize(void) { RET(init(A32(0), COINIT_APARTMENTTHREADED), 1); }
static void k_CoInitializeEx(void) { RET(init(A32(0), A32(1)), 2); }
static void k_CoUninitialize(void) {
    int i = slot(thread_id(), 0);
    if (i >= 0) g_thr[i].count--;
    RETV(0);
}

int tm_ole32_apartment(void) {
    int i = slot(thread_id(), 0);
    return i < 0 ? 0 : g_thr[i].model;
}

const win32hle_shim tm_ole32_overrides[] = {
    { "tm:CoInitialize",   k_CoInitialize },
    { "tm:CoInitializeEx", k_CoInitializeEx },
    { "tm:CoUninitialize", k_CoUninitialize },
    { 0, 0 }
};
