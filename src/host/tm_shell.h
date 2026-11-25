#ifndef TM_SHELL_H
#define TM_SHELL_H
#include "win32hle.h"

/* SHGetFolderPathW, SHGetSpecialFolderPathA */
extern const win32hle_shim tm_shell_shims[];

/* Map guest drive D: to user_root (created): every shell folder lives there. */
void tm_shell_init(const char *user_root);

#endif
