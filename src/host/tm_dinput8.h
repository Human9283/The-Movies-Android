#ifndef TM_DINPUT8_H
#define TM_DINPUT8_H
#include <stdio.h>

/* Registers dinput8.dll as a host DLL (tm_loader.h) with its COM vtables. */
void tm_dinput8_init(void);
/* Per-method call counts. */
void tm_dinput8_report(FILE *f);

#endif
