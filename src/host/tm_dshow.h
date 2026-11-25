#ifndef TM_DSHOW_H
#define TM_DSHOW_H
#include <stdio.h>

/* Registers the COM classes of the game's DirectShow start-up probe
 * (CLSID_FilterGraph, CLSID_CaptureGraphBuilder2, CLSID_WMAsfWriter) with
 * win32hle's CoCreateInstance. Only the probe's contract is implemented:
 * docs/DIRECTSHOW-STARTUP.md. */
void tm_dshow_init(void);
/* Objects alive now (all three kinds). */
int  tm_dshow_live(void);
/* Objects created and destroyed, per class. */
void tm_dshow_report(FILE *f);

#endif
