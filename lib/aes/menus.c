/* Standard GEM menu item APIs over caller-owned object trees.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "application.h"
#include "menu_bindings.h"
#include "string.h"

WORD *aes_menu_global;

static WORD change(OBJECT *tree, WORD item, UWORD bit, WORD set,
                   WORD draw, WORD check_disabled, WORD *global)
{
    WORD *saved = aes_menu_global, result;
    if (!tree || item < 0 || aes_application_session(global) < 0) return 0;
    aes_menu_global = global;
    result = aes_menu_change_core(tree,item,bit,set,draw,check_disabled);
    aes_menu_global = saved;
    return result;
}
WORD mt_menu_icheck(OBJECT *tree, WORD item, WORD check, WORD *global)
{ return change(tree,item,CHECKED,check,FALSE,FALSE,global); }
WORD menu_icheck(OBJECT *tree, WORD item, WORD check)
{ return mt_menu_icheck(tree,item,check,aes_global); }
WORD mt_menu_ienable(OBJECT *tree, WORD item, WORD enable, WORD *global)
{ return change(tree,item & 0x7fff,DISABLED,!enable,((UWORD)item & 0x8000) != 0,FALSE,global); }
WORD menu_ienable(OBJECT *tree, WORD item, WORD enable)
{ return mt_menu_ienable(tree,item,enable,aes_global); }
WORD mt_menu_tnormal(OBJECT *tree, WORD item, WORD normal, WORD *global)
{ return change(tree,item,SELECTED,!normal,TRUE,TRUE,global); }
WORD menu_tnormal(OBJECT *tree, WORD item, WORD normal)
{ return mt_menu_tnormal(tree,item,normal,aes_global); }
WORD mt_menu_text(OBJECT *tree, WORD item, const char *text, WORD *global)
{
    if (!tree || item < 0 || !text || aes_application_session(global) < 0 ||
        !tree[item].ob_spec.free_string) return 0;
    /* Like native AES, copy into the existing caller-owned menu buffer.
     * The caller supplies enough room, including the terminating NUL. */
    strcpy(tree[item].ob_spec.free_string,text);
    return 1;
}
WORD menu_text(OBJECT *tree, WORD item, const char *text)
{ return mt_menu_text(tree,item,text,aes_global); }
