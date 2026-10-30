/* Kernel-private CUIS entry points. */
#ifndef CUIS_KERNEL_H
#define CUIS_KERNEL_H
#include "bdosdefs.h"
#include "cuis_region.h"
#include "../include/cuis.h"
LONG cuis_surface_service(ULONG function, UQUAD session, UQUAD argument);
LONG cuis_input_config(ULONG function, UQUAD session, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e);
LONG cuis_notice_service(ULONG function, UQUAD session, UQUAD a, UQUAD b, UQUAD c, UQUAD d);
LONG cuis_stack_service(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e);
LONG cuis_window_at(UQUAD x, UQUAD y);
LONG cuis_keyboard_lock(UQUAD handle, UQUAD acquire);
LONG cuis_display_redraw(UQUAD handle, UQUAD x, UQUAD y, UQUAD width, UQUAD height);
LONG cuis_mouse_lock(UQUAD handle, UQUAD acquire, UQUAD block);
LONG cuis_display_lock(UQUAD handle, UQUAD acquire, UQUAD block);
LONG cuis_region_service(UQUAD handle, UQUAD index, UQUAD x, UQUAD y, UQUAD width, UQUAD height);
LONG cuis_service(ULONG function, UQUAD handle);
LONG cuis_window_service(ULONG function, UQUAD a, UQUAD b, UQUAD c,
                         UQUAD d, UQUAD e);
LONG cuis_clip_service(UQUAD handle, UQUAD index, UQUAD x, UQUAD y,
                       UQUAD width, UQUAD height);
LONG cuis_pointer_style(UQUAD handle, UQUAD form, UQUAD hidden);
void cuis_pointer_sync(void);
LONG cuis_vdi_policy(void);
BOOL cuis_vdi_clip(ULONG index, CUIS_RECT *rect);
BOOL cuis_keyboard_event(ULONG key);
LONG cuis_input_service(ULONG function, UQUAD handle, UQUAD argument);
BOOL cuis_pointer_packet(WORD dx, WORD dy, ULONG buttons, LONG *x, LONG *y);
void cuis_mouse_packet(SBYTE *packet);
LONG cuis_control_service(UQUAD handle, UQUAD x, UQUAD y, UQUAD buttons, UQUAD changed);
LONG cuis_pointer_service(UQUAD handle, UQUAD x, UQUAD y, UQUAD buttons, UQUAD changed);
LONG cuis_message_service(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f);
LONG cuis_event_service(UQUAD handle, UQUAD events, UQUAD timeout);
LONG cuis_delay_service(UQUAD handle, UQUAD milliseconds);
LONG cuis_directory_service(ULONG function, UQUAD a, UQUAD b, UQUAD c);
LONG cuis_stream_service(ULONG function, UQUAD handle, UQUAD buffer, UQUAD length,
                         UQUAD block, UQUAD sender);
LONG cuis_session_wait(UQUAD handle, UQUAD minimum, UQUAD events, UQUAD milliseconds);
LONG cuis_state_service(UQUAD handle, UQUAD x, UQUAD y, UQUAD buttons, UQUAD modifiers);
void cuis_process_exit(PD *owner);
#if CONF_WITH_X86_64_MEMTEST
ULONG cuis_session_count(void);
BOOL cuis_update_active(void);
BOOL cuis_mouse_active(void);
BOOL cuis_focus_active(void);
BOOL cuis_pointer_attached(void);
ULONG cuis_window_count(void);
ULONG cuis_visible_window_count(void);
#endif
#endif
