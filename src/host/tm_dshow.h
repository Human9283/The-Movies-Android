#ifndef TM_DSHOW_H
#define TM_DSHOW_H
#include <stdio.h>

/* Registers the DirectShow probe classes and graph media interfaces
 * with win32hle's COM runtime. The ASF source filter and media pipeline are
 * separate capabilities. */
void tm_dshow_init(void);
/* Objects alive now (all three kinds). */
int  tm_dshow_live(void);
/* Objects created and destroyed, per class. */
void tm_dshow_report(FILE *f);

#endif
