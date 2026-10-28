/* Standard GEM popup-menu descriptor. */
#ifndef GEMMENU_H
#define GEMMENU_H
#include "obdefs.h"
typedef struct {
    OBJECT *mn_tree;
    WORD mn_menu, mn_item, mn_scroll, mn_keystate;
} MENU;
#endif
