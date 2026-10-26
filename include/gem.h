/* Public GEM AES bindings for the implemented libaes subset.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 * Window/input event adapters follow in later slices. */
#ifndef PTOS_GEM_H
#define PTOS_GEM_H
#include "obdefs.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef OS_DISABLED
#define OS_DISABLED DISABLED
#endif
#ifndef OS_SHADOWED
#define OS_SHADOWED SHADOWED
#endif
#ifndef OF_LASTOB
#define OF_LASTOB LASTOB
#endif
#ifndef OF_HIDETREE
#define OF_HIDETREE HIDETREE
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
#ifndef WM_REDRAW
#define WM_REDRAW 20
#endif
#ifndef WM_TOPPED
#define WM_TOPPED 21
#endif
#ifndef WF_OWNER
#define WF_OWNER 20
#endif
#ifndef END_UPDATE
#define END_UPDATE 0
#endif
#ifndef BEG_UPDATE
#define BEG_UPDATE 1
#endif
#ifndef END_MCTRL
#define END_MCTRL 2
#endif
#ifndef BEG_MCTRL
#define BEG_MCTRL 3
#endif
#ifndef NO_BLOCK
#define NO_BLOCK 0x0100
#endif
#ifndef WF_FIRSTXYWH
#define WF_FIRSTXYWH 11
#endif
#ifndef WF_NEXTXYWH
#define WF_NEXTXYWH 12
#endif
short wind_update(short code);
short mt_wind_update(short code, short *global);
#ifndef WC_BORDER
#define WC_BORDER 0
#endif
#ifndef WC_WORK
#define WC_WORK 1
#endif
#ifndef WF_KIND
#define WF_KIND 1
#endif
#ifndef WF_WORKXYWH
#define WF_WORKXYWH 4
#endif
#ifndef WF_CURRXYWH
#define WF_CURRXYWH 5
#endif
#ifndef WF_PREVXYWH
#define WF_PREVXYWH 6
#endif
#ifndef WF_FULLXYWH
#define WF_FULLXYWH 7
#endif
#ifndef WF_TOP
#define WF_TOP 10
#endif
#ifndef WF_BOTTOM
#define WF_BOTTOM 25
#endif
short wind_create(short kind, short x, short y, short w, short h);
short mt_wind_create(short kind, short x, short y, short w, short h, short *global);
short wind_open(short id, short x, short y, short w, short h);
short mt_wind_open(short id, short x, short y, short w, short h, short *global);
short wind_close(short id);
short mt_wind_close(short id, short *global);
short wind_delete(short id);
short mt_wind_delete(short id, short *global);
short wind_get(short id, short field, short *a, short *b, short *c, short *d);
short mt_wind_get(short id, short field, short *a, short *b, short *c, short *d, short *global);
short wind_set(short id, short field, short a, short b, short c, short d);
short mt_wind_set(short id, short field, short a, short b, short c, short d, short *global);
short wind_calc(short type, short kind, short x, short y, short w, short h, short *ox, short *oy, short *ow, short *oh);
short mt_wind_calc(short type, short kind, short x, short y, short w, short h, short *ox, short *oy, short *ow, short *oh, short *global);
short objc_find(OBJECT *tree, short start, short depth, short mx, short my);
short mt_objc_find(OBJECT *tree, short start, short depth, short mx, short my, short *global);
short mt_objc_add(OBJECT *tree, short parent, short child, short *global);
short mt_objc_delete(OBJECT *tree, short object, short *global);
short mt_objc_order(OBJECT *tree, short object, short position, short *global);
short mt_objc_offset(OBJECT *tree, short object, short *x, short *y, short *global);
short objc_add(OBJECT *tree, short parent, short child);
short objc_delete(OBJECT *tree, short object);
short objc_order(OBJECT *tree, short object, short position);
short objc_offset(OBJECT *tree, short object, short *x, short *y);
#ifdef __cplusplus
}
#endif
#endif
