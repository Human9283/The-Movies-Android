/*
 * tm_winmm.c - the winmm.dll functions the game reaches that win32hle does
 * not answer, in win32hle's model of this machine's sound devices: wave
 * output through win32hle's player, no wave input (recording) device and no
 * mixer device (win32hle winmm.c: waveInGetNumDevs and mixerGetNumDevs
 * return 0, waveInOpen and mixerOpen MMSYSERR_BADDEVICEID).
 *
 * waveInGetDevCapsA(uDeviceID, pwic, cbwic): with no input device there is
 * no device to describe, the wave mapper (WAVE_MAPPER, -1) included, so
 * every identifier is MMSYSERR_BADDEVICEID and *pwic is not written. A NULL
 * pwic is MMSYSERR_INVALPARAM, checked first, as Wine's winmm does.
 * [hyp.] That Windows XP answers MMSYSERR_BADDEVICEID for WAVE_MAPPER with
 * no recording device is not checked on Windows; the game takes any
 * non-zero result as "no recording" (sub_009B5B00 returns false).
 */
#include <stdint.h>
#include <stdio.h>
#include "win32hle.h"
#include "tm_winmm.h"

#define MMSYSERR_BADDEVICEID 2u
#define MMSYSERR_INVALPARAM  11u

static void m_waveInGetDevCapsA(void) {             /* (uDeviceID, pwic, cbwic) */
    uint32_t id = A32(0), caps = A32(1), size = A32(2);
    uint32_t r = caps ? MMSYSERR_BADDEVICEID : MMSYSERR_INVALPARAM;
    static int logged;
    if (logged++ < 4)
        fprintf(stderr, "[winmm] waveInGetDevCapsA(%s%u, 0x%08X, %u) -> %u: no wave input device\n",
                id == 0xFFFFFFFFu ? "WAVE_MAPPER " : "", id, caps, size, r);
    RET(r, 3);
}

const win32hle_shim tm_winmm_overrides[] = {
    { "tm:waveInGetDevCapsA", m_waveInGetDevCapsA },
    { 0, 0 }
};
