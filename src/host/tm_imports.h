/*
 * tm_imports.h - what each import the game binds to actually is
 * (docs/IMPORTS.md). Every import has exactly one status, printed at boot and
 * written by --imports-json:
 *
 *   implemented        our own implementation (src/host), written against
 *                      the Windows documentation: an input it does not
 *                      handle fails the way Windows fails, never with an
 *                      invented success.
 *   diagnostic         lets the run go on, but not as Windows would (no
 *                      display: a message box is printed and auto-answered).
 *                      Never on a critical path.
 *   win32hle-present   answered by pcrecomp's win32hle; not yet checked
 *                      against the calls this game makes.
 *   win32hle-verified  answered by win32hle and checked against the game's
 *                      recorded calls (docs/IMPORTS.md, with a test).
 *   unsupported        no implementation: calling it stops the run.
 */
#ifndef TM_IMPORTS_H
#define TM_IMPORTS_H

typedef enum {
    TM_IMPLEMENTED, TM_DIAGNOSTIC, TM_WIN32HLE_PRESENT, TM_WIN32HLE_VERIFIED, TM_UNSUPPORTED,
    TM_NSTATUS
} tm_import_status;

typedef struct {
    const char      *name;
    tm_import_status status;
    const char      *note;    /* what it does, where it is not Windows, or the test that verifies it */
} tm_import_info;

/* Our own shims (src/host) and the win32hle imports verified so far,
 * NULL-terminated. */
extern const tm_import_info tm_import_table[];

const char *tm_import_status_name(tm_import_status s);
/* The status the table gives a name, or -1 if the table does not list it. */
int tm_import_listed_status(const char *name, const char **note);

#endif
