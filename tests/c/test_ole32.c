/*
 * COM initialisation per thread (src/host/tm_ole32.c): first S_OK, then
 * S_FALSE, the other model RPC_E_CHANGED_MODE, CoUninitialize undoing one
 * call, threads apart. No game files.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "win32hle.h"
#include "host.h"
#include "tm_ole32.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}
static uint32_t call(const char *name, int n, uint32_t a0, uint32_t a1) {
    uint32_t a[2] = { a0, a1 };
    return hle_call_guest(hle_resolve(name), n, a);
}
#define S_FALSE 1u
#define RPC_E_CHANGED_MODE 0x80010106u
#define E_INVALIDARG 0x80070057u

static uint32_t g_other[3];
static void *other_thread(void *p) {
    (void)p;
    mach_enter();
    g_other[0] = call("tm:CoInitializeEx", 2, 0, 0);          /* multithreaded, its own thread */
    g_other[1] = (uint32_t)tm_ole32_apartment();
    call("tm:CoUninitialize", 0, 0, 0);
    g_other[2] = (uint32_t)tm_ole32_apartment();
    mach_leave();
    return NULL;
}

int main(void) {
    recomp_host_init();
    win32hle_register(tm_ole32_overrides);
    mach_enter();
    check("no apartment at first", tm_ole32_apartment() == 0);
    check("CoInitialize(NULL): S_OK", call("tm:CoInitialize", 1, 0, 0) == 0);
    check("  single-threaded apartment", tm_ole32_apartment() == 1);
    check("CoInitialize again: S_FALSE", call("tm:CoInitialize", 1, 0, 0) == S_FALSE);
    check("CoInitializeEx(APARTMENTTHREADED): S_FALSE", call("tm:CoInitializeEx", 2, 0, 2) == S_FALSE);
    check("CoInitializeEx(MULTITHREADED) on it: RPC_E_CHANGED_MODE", call("tm:CoInitializeEx", 2, 0, 0) == RPC_E_CHANGED_MODE);
    check("  still single-threaded", tm_ole32_apartment() == 1);
    check("CoInitialize with a reserved pointer: E_INVALIDARG", call("tm:CoInitialize", 1, 0x1000, 0) == E_INVALIDARG);
    call("tm:CoUninitialize", 0, 0, 0);
    call("tm:CoUninitialize", 0, 0, 0);
    check("two of three undone: still in it", tm_ole32_apartment() == 1);
    call("tm:CoUninitialize", 0, 0, 0);
    check("the third: left", tm_ole32_apartment() == 0);
    call("tm:CoUninitialize", 0, 0, 0);
    check("CoUninitialize with none: nothing", tm_ole32_apartment() == 0);
    check("CoInitializeEx(MULTITHREADED) now: S_OK", call("tm:CoInitializeEx", 2, 0, 0) == 0 && tm_ole32_apartment() == 2);

    mach_leave();
    pthread_t t;
    pthread_create(&t, NULL, other_thread, NULL);
    pthread_join(t, NULL);
    mach_enter();
    check("another thread: its own first S_OK", g_other[0] == 0 && g_other[1] == 2 && g_other[2] == 0);
    check("  this thread unchanged", tm_ole32_apartment() == 2);
    call("tm:CoUninitialize", 0, 0, 0);
    mach_leave();
    printf("ole32: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
