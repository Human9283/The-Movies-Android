/* Compile-time ABI guard for the AArch64 dispatch-table assembler. */
#include <stddef.h>
#include "recomp_types.h"

_Static_assert(sizeof(void *) == 8, "Android dispatch table requires 64-bit pointers");
_Static_assert(offsetof(recomp_dispatch_entry_t, address) == 0, "dispatch address offset changed");
_Static_assert(offsetof(recomp_dispatch_entry_t, func) == 8, "dispatch function pointer offset changed");
_Static_assert(sizeof(recomp_dispatch_entry_t) == 16, "dispatch entry size changed");
