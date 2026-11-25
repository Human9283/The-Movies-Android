/*
 * winmm.dll functions win32hle does not answer (src/host/tm_winmm.c):
 * waveInGetDevCapsA in win32hle's model of no wave input device. No game
 * files.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "win32hle.h"
#include "host.h"
#include "tm_winmm.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, 0}};
const uint32_t recomp_dispatch_count = 0;

static int failures, checks;
static void check(const char *label, int ok) {
    checks++;
    if (!ok) { fprintf(stderr, "FAIL %s\n", label); failures++; }
}
static uint32_t call(const char *name, int n, uint32_t a0, uint32_t a1, uint32_t a2) {
    uint32_t a[3] = { a0, a1, a2 };
    return hle_call_guest(hle_resolve(name), n, a);
}
#define WAVE_MAPPER 0xFFFFFFFFu
#define MMSYSERR_BADDEVICEID 2u
#define MMSYSERR_INVALPARAM 11u

int main(void) {
    recomp_host_init();
    win32hle_register(tm_winmm_overrides);
    mach_enter();
    uint32_t caps = hle_va(hle_alloc(0x40));
    memset(hle_ptr(caps), 0xAB, 0x40);
    check("win32hle: waveInGetNumDevs is 0", call("waveInGetNumDevs", 0, 0, 0, 0) == 0);
    check("WAVE_MAPPER: MMSYSERR_BADDEVICEID", call("tm:waveInGetDevCapsA", 3, WAVE_MAPPER, caps, 48) == MMSYSERR_BADDEVICEID);
    check("device 0: MMSYSERR_BADDEVICEID", call("tm:waveInGetDevCapsA", 3, 0, caps, 48) == MMSYSERR_BADDEVICEID);
    int untouched = 1;
    for (int i = 0; i < 0x40; i++) untouched &= ((uint8_t *)hle_ptr(caps))[i] == 0xAB;
    check("  WAVEINCAPSA not written", untouched);
    check("NULL caps: MMSYSERR_INVALPARAM", call("tm:waveInGetDevCapsA", 3, WAVE_MAPPER, 0, 48) == MMSYSERR_INVALPARAM);
    uint32_t h = hle_va(hle_alloc(4));
    MEM32(h) = 0x1234;
    check("win32hle: waveInOpen(WAVE_MAPPER) agrees: MMSYSERR_BADDEVICEID",
          hle_call_guest(hle_resolve("waveInOpen"), 6, (uint32_t[]){ h, WAVE_MAPPER, caps, 0, 0, 0 }) == MMSYSERR_BADDEVICEID
          && MEM32(h) == 0);
    mach_leave();
    printf("winmm: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
