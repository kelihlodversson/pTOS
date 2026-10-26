/* Public GEM AES bindings for the implemented libaes subset.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 * Window/input event adapters follow in later slices. */
#ifndef PTOS_GEM_H
#define PTOS_GEM_H
#include "obdefs.h"

#ifdef __cplusplus
extern "C" {
#endif
#define AES_GLOBMAX 15
#define AES_PROCESS 4
#define AES_INQUIRE 6
#define APR_NOWAIT (-1)
#define MU_KEYBD 0x0001
#define MU_BUTTON 0x0002
#define MU_M1 0x0004
#define MU_M2 0x0008
#define MU_MESAG 0x0010
#define MU_TIMER 0x0020
extern short aes_global[AES_GLOBMAX];
extern short gl_apid, gl_ap_version;
#define _AESversion (aes_global[0])
#define _AESnumapps (aes_global[1])
#define _AESapid (aes_global[2])
short appl_init(void);
short appl_exit(void);
short appl_yield(void);
short evnt_timer(unsigned long interval);
short appl_getinfo(short type, short *out1, short *out2, short *out3, short *out4);
short appl_find(const char *name);
short appl_write(short id, short length, void *buffer);
short appl_read(short id, short length, void *buffer);
short evnt_mesag(short message[8]);
short mt_appl_init(short *global_aes);
short mt_appl_exit(short *global_aes);
short mt_appl_yield(short *global_aes);
short mt_evnt_timer(unsigned long interval, short *global_aes);
short mt_appl_getinfo(short type, short *out1, short *out2, short *out3, short *out4, short *global_aes);
short mt_appl_find(const char *name, short *global_aes);
short mt_appl_write(short id, short length, void *buffer, short *global_aes);
short mt_appl_read(short id, short length, void *buffer, short *global_aes);
short mt_evnt_mesag(short message[8], short *global_aes);
short evnt_multi(short events, short clicks, short button_mask, short button_state,
                 short m1_leave, short m1_x, short m1_y, short m1_w, short m1_h,
                 short m2_leave, short m2_x, short m2_y, short m2_w, short m2_h,
                 short message[], unsigned long interval, short *mx, short *my,
                 short *buttons, short *modifiers, short *key, short *click_count);
short mt_evnt_multi(short events, short clicks, short button_mask, short button_state,
                    short m1_leave, short m1_x, short m1_y, short m1_w, short m1_h,
                    short m2_leave, short m2_x, short m2_y, short m2_w, short m2_h,
                    short message[], unsigned long interval, short *mx, short *my,
                    short *buttons, short *modifiers, short *key, short *click_count, short *global_aes);
short graf_mkstate(short *mx, short *my, short *buttons, short *modifiers);
short mt_graf_mkstate(short *mx, short *my, short *buttons, short *modifiers, short *global_aes);
WORD objc_add(OBJECT *tree, WORD parent, WORD child);
WORD objc_delete(OBJECT *tree, WORD object);
WORD objc_order(OBJECT *tree, WORD object, WORD position);
WORD objc_offset(OBJECT *tree, WORD object, WORD *x, WORD *y);
#ifdef __cplusplus
}
#endif
#endif
