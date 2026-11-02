/* Private GEM lifecycle adaptation over opaque native cohort requests. */
#ifndef LIBAES_LIFECYCLE_H
#define LIBAES_LIFECYCLE_H
#include "portab.h"
void aes_lifecycle_reset(BOOL accessory);
BOOL aes_lifecycle_listening(void);
LONG aes_lifecycle_messages(WORD *global);
WORD aes_lifecycle_write(WORD mode, WORD flags, WORD action, const void *command, WORD *global);
#endif
