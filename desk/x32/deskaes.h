/* Local classic binding adapter. No AESPB crosses into the kernel. */
#ifndef DESK_X32_AES_H
#define DESK_X32_AES_H
#include "obdefs.h"
/* Classic window argument/result arrays contain at least 8/7 WORDs. */
LONG desk_window(UWORD opcode, const UWORD *in, UWORD *out);
WORD desk_aes_dispatch(UWORD opcode, UWORD *global, UWORD *in, UWORD *out,
                       intptr_t *addresses, intptr_t *results);
#if CONF_WITH_X32_DESKTOP_TEST
void desk_test_record(const char *message);
#else
#define desk_test_record(message) ((void)0)
#endif

#endif
