#ifndef LIBAES_MENUBAR_H
#define LIBAES_MENUBAR_H
#include "portab.h"
BOOL aes_menu_registered(void);
BOOL aes_menu_listening(void);
LONG aes_menu_flush(WORD *global);
LONG aes_menu_dispatch(WORD *global);
void aes_menu_reset(void);
#endif
