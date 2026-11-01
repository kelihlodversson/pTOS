/* Bind ordinary shared form C units to the active userspace gesture. */
#ifndef LIBAES_FORM_BINDINGS_H
#define LIBAES_FORM_BINDINGS_H
#include "form_context.h"
#define fm_button aes_form_button_core
#define gr_watchbox aes_form_watch_core
/* Calls inside form_button enter the watch adapter, not a second public call. */
#ifdef AES_FORM_BUTTON_UNIT
#undef gr_watchbox
#define gr_watchbox aes_form_watch
#endif
#define gl_rscreen (aes_form_context->screen)
#define gsx_sclip aes_form_clip
#define ob_actxywh aes_form_rect
#define ob_fs aes_form_flags
#define ob_change aes_form_change
#define gr_stilldn aes_form_stilldn
#define ev_button aes_form_release_wait
#endif
