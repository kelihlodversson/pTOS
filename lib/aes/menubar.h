#ifndef LIBAES_MENUBAR_H
#define LIBAES_MENUBAR_H
#include "portab.h"
#include "aesglobal.h"
BOOL aes_menu_registered(void);
BOOL aes_menu_listening(void);
LONG aes_menu_flush(AESGLOBAL *global);
LONG aes_menu_dispatch(AESGLOBAL *global);
void aes_menu_reset(void);
#endif
