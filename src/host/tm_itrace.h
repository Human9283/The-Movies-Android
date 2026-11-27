#ifndef TM_ITRACE_H
#define TM_ITRACE_H
#include <stdint.h>

/* Call before the bind: from then on tm_itrace_wrap returns a recording
 * trampoline instead of the import's own VA. */
void tm_itrace_enable(void);
int  tm_itrace_enabled(void);

/* The VA to put in the IAT for an import whose shim is at real_va. */
uint32_t tm_itrace_wrap(const char *name, uint32_t real_va, int status);

/* 1 if the trampoline table maps index i to itrace_call(i). */
int tm_itrace_selfcheck(void);

/* Every import called at least once, as JSON (docs/IMPORTS.md). */
void tm_itrace_write(const char *path, const char *(*status_name)(int));

#endif
