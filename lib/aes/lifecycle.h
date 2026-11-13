/* Private GEM lifecycle adaptation over opaque native cohort requests. */
#ifndef LIBAES_LIFECYCLE_H
#define LIBAES_LIFECYCLE_H
#include "portab.h"
#include "aesglobal.h"
void aes_lifecycle_reset(BOOL accessory, AESGLOBAL *global);
BOOL aes_lifecycle_listening(AESGLOBAL *global);
LONG aes_lifecycle_messages(AESGLOBAL *global);
BOOL aes_lifecycle_wait_display(AESGLOBAL *global);
LONG aes_lifecycle_read(void *buffer, LONG bytes, AESGLOBAL *global, BOOL polling);
LONG aes_lifecycle_deliver(void *buffer, LONG bytes, AESGLOBAL *global, BOOL polling);
WORD aes_lifecycle_write(WORD mode, WORD flags, WORD action, const void *command, AESGLOBAL *global);
#endif
