/* Private registration access for standard GEM adapters. */
#ifndef LIBAES_APPLICATION_H
#define LIBAES_APPLICATION_H
#include "portab.h"
LONG aes_window_messages(LONG session, short *global);
void aes_windows_reset(void);
LONG aes_application_session(short *global);
#endif
