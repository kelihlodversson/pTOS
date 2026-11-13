/* Private registration access for standard GEM adapters. */
#ifndef LIBAES_APPLICATION_H
#define LIBAES_APPLICATION_H
#include "portab.h"
#include "aesglobal.h"
LONG aes_window_messages(LONG session, AESGLOBAL *global);
LONG aes_window_messages_poll(LONG session, AESGLOBAL *global);
BOOL aes_resources_reset(AESGLOBAL *global);
void aes_windows_reset(AESGLOBAL *global);
LONG aes_windows_reset_policy(AESGLOBAL *global, BOOL polling, LONG token);
BOOL aes_windows_pending(AESGLOBAL *global);
BOOL aes_controls_listening(AESGLOBAL *global);
BOOL aes_controls_repeating(AESGLOBAL *global);
BOOL aes_frames_registered(AESGLOBAL *global);
BOOL aes_frames_listening(AESGLOBAL *global);
BOOL aes_frames_suspended(AESGLOBAL *global);
BOOL aes_frames_wait_display(AESGLOBAL *global);
BOOL aes_frames_suspend(AESGLOBAL *global);
void aes_frames_resume(AESGLOBAL *global);
BOOL aes_frames_dialog(BOOL begin, AESGLOBAL *global);
LONG aes_application_session(AESGLOBAL *global);
LONG aes_child_messages(LONG session);
#endif
