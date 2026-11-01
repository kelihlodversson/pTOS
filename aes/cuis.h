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
LONG cuis_background(UQUAD session, UQUAD action);
LONG cuis_background_region(UQUAD session, UQUAD index, UQUAD x, UQUAD y, UQUAD width, UQUAD height);
LONG cuis_display_info(UQUAD selector);
LONG cuis_windows_reset(UQUAD session);
#if CONF_WITH_X86_64_MEMTEST
BOOL cuis_context_test_reset(void);
#endif
LONG cuis_context(UQUAD session, UQUAD slot, UQUAD action, UQUAD address, UQUAD bytes, UQUAD offset);
LONG cuis_setting(UQUAD slot, UQUAD value, UQUAD other, UQUAD other_value, UQUAD mask);
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
LONG cuis_child_service(ULONG function, UQUAD session, UQUAD a, UQUAD b, UQUAD c);
void cuis_child_completed(PD *parent, WORD pid, UWORD status);
void cuis_child_fault(PD *parent, WORD pid, WORD signal);
LONG cuis_child_launch(UQUAD session, UQUAD token, UQUAD path, UQUAD tail, UQUAD environment);
LONG cuis_child_bind_spawn(ULONG session, ULONG token, PD *child);
BOOL cuis_child_pid_reserved(WORD pid);
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
