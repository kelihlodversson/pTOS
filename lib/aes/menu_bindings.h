/* Compile the native menu state routine against ordinary GEM calls. */
#ifndef LIBAES_MENU_BINDINGS_H
#define LIBAES_MENU_BINDINGS_H
#include "gem.h"
extern WORD *aes_menu_global;
#define do_chg aes_menu_change_core
/* The public draw adapter supplies the full screen clip itself. */
#define gsx_sclip(rect) ((void)0)
#define MENU_APPLY(tree,item,state,draw) \
    mt_objc_change(tree,item,0,0,0,32767,32767,state,draw,aes_menu_global)
BOOL aes_menu_change_core(OBJECT *tree, WORD item, UWORD bit,
                         WORD set, WORD draw, WORD check_disabled);
#endif
