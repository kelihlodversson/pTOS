/* Bind the original form loop to caller-local GEM entry points. */
#include "modal_context.h"
#define fm_do aes_form_do_core
#define ob_edit aes_modal_edit
#define ob_find(tree,start,depth,x,y) mt_objc_find(tree,start,depth,x,y,aes_modal->global)
#define fm_keybd aes_modal_keybd
#define fm_button aes_modal_button
#define FM_DO_ENTER() aes_modal_enter()
#define FM_DO_LEAVE() aes_modal_leave()
/* Entering keyboard capture flushes its fresh queue atomically. */
#define FM_DO_FLUSH() ((void)0)
#define FM_DO_CLIP() ((void)0)
#define FM_DO_BELL() ((void)0)
#define FM_DO_OK() (aes_modal->ok)
#define FM_DO_WAIT(rets) aes_modal_wait(rets)
