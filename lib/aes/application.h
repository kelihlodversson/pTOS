/* Private registration access for standard GEM adapters. */
#ifndef LIBAES_APPLICATION_H
#define LIBAES_APPLICATION_H
#include "portab.h"
LONG aes_window_messages(LONG session, short *global);
LONG aes_window_messages_poll(LONG session, short *global);
void aes_resources_reset(void);
void aes_windows_reset(void);
BOOL aes_controls_listening(void);
BOOL aes_controls_repeating(void);
BOOL aes_frames_registered(void);
BOOL aes_frames_listening(void);
BOOL aes_frames_suspended(void);
BOOL aes_frames_wait_display(void);
BOOL aes_frames_suspend(void);
void aes_frames_resume(void);
BOOL aes_frames_dialog(BOOL begin);
LONG aes_application_session(short *global);
LONG aes_child_messages(LONG session);
#endif
