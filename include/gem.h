/* Public GEM AES bindings for the implemented libaes subset.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 * Further resource, colour-image and decoration adapters follow in later slices. */
#ifndef PTOS_GEM_H
#define PTOS_GEM_H
#include "obdefs.h"
#include "rsdefs.h"
#include "gemmenu.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef OS_WHITEBAK
#define OS_WHITEBAK WHITEBAK
#endif
#ifndef OS_DISABLED
#define OS_DISABLED DISABLED
#endif
#ifndef OS_SHADOWED
#define OS_SHADOWED SHADOWED
#endif
#ifndef OF_TOUCHEXIT
#define OF_TOUCHEXIT TOUCHEXIT
#endif
#ifndef OF_RBUTTON
#define OF_RBUTTON RBUTTON
#endif
#ifndef OF_EDITABLE
#define OF_EDITABLE EDITABLE
#endif
#ifndef OF_LASTOB
#define OF_LASTOB LASTOB
#endif
#ifndef OF_INDIRECT
#define OF_INDIRECT INDIRECT
#endif
#ifndef OF_HIDETREE
#define OF_HIDETREE HIDETREE
#endif
#ifndef OS_SELECTED
#define OS_SELECTED SELECTED
#endif
#ifndef OS_CROSSED
#define OS_CROSSED CROSSED
#endif
#ifndef OS_OUTLINED
#define OS_OUTLINED OUTLINED
#endif
#ifndef OS_CHECKED
#define OS_CHECKED CHECKED
#endif
#ifndef OF_SELECTABLE
#define OF_SELECTABLE SELECTABLE
#endif
#ifndef OF_EXIT
#define OF_EXIT EXIT
#endif
#ifndef OF_DEFAULT
#define OF_DEFAULT DEFAULT
#endif
#define AES_GLOBMAX 15
#define AES_PROCESS 4
#define AES_INQUIRE 6
#define AES_OBJECT 13
#define SV_INQUIRE 0
#define SV_SET 1
#define AD3DVAL AD3DVALUE
#define APR_NOWAIT (-1)
#define MU_KEYBD 0x0001
#define MU_BUTTON 0x0002
#define MU_M1 0x0004
#define MU_M2 0x0008
#define MU_MESAG 0x0010
#define MU_TIMER 0x0020
#ifndef MO_ENTER
#define MO_ENTER 0
#define MO_LEAVE 1
#endif
#ifndef LEFT_BUTTON
#define LEFT_BUTTON 1
#define RIGHT_BUTTON 2
#endif
#ifndef K_RSHIFT
#define K_RSHIFT 1
#define K_LSHIFT 2
#define K_CTRL 4
#define K_ALT 8
#endif
#ifndef EDC_INQUIRE
#define EDC_INQUIRE 0
#define EDC_SET 1
#endif
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
short evnt_keybd(void);
short mt_evnt_keybd(short *global_aes);
short evnt_button(short clicks, short mask, short state, short *mx, short *my, short *buttons, short *modifiers);
short mt_evnt_button(short clicks, short mask, short state, short *mx, short *my, short *buttons, short *modifiers, short *global_aes);
short evnt_mouse(short leave, short x, short y, short width, short height, short *mx, short *my, short *buttons, short *modifiers);
short mt_evnt_mouse(short leave, short x, short y, short width, short height, short *mx, short *my, short *buttons, short *modifiers, short *global_aes);
short evnt_dclick(short rate, short set);
short mt_evnt_dclick(short rate, short set, short *global_aes);
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
#ifndef NAME
#define NAME 0x0001
#endif
#ifndef CLOSER
#define CLOSER 0x0002
#endif
#ifndef FULLER
#define FULLER 0x0004
#endif
#ifndef MOVER
#define MOVER 0x0008
#endif
#ifndef INFO
#define INFO 0x0010
#endif
#ifndef SIZER
#define SIZER 0x0020
#endif
#ifndef UPARROW
#define UPARROW 0x0040
#endif
#ifndef DNARROW
#define DNARROW 0x0080
#endif
#ifndef VSLIDE
#define VSLIDE 0x0100
#endif
#ifndef LFARROW
#define LFARROW 0x0200
#endif
#ifndef RTARROW
#define RTARROW 0x0400
#endif
#ifndef HSLIDE
#define HSLIDE 0x0800
#endif
#ifndef HOTCLOSE
#define HOTCLOSE 0x1000
#endif
#ifndef HOTCLOSEBOX
#define HOTCLOSEBOX 0x1000
#endif
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
#ifndef ED_START
#define ED_START 0
#define ED_INIT 1
#define ED_CHAR 2
#define ED_END 3
#endif
#ifndef FMD_START
#define FMD_START 0
#define FMD_GROW 1
#define FMD_SHRINK 2
#define FMD_FINISH 3
#endif
short objc_edit(OBJECT *tree, short object, short key, short *index, short kind);
short mt_objc_edit(OBJECT *tree, short object, short key, short *index, short kind, short *global);
short form_do(OBJECT *tree, short start);
short mt_form_do(OBJECT *tree, short start, short *global);
short form_dial(short kind, short sx, short sy, short sw, short sh, short x, short y, short w, short h);
short mt_form_dial(short kind, short sx, short sy, short sw, short sh, short x, short y, short w, short h, short *global);
short form_alert(short default_button, const char *alert);
short mt_form_alert(short default_button, const char *alert, short *global);
short form_error(short error);
short mt_form_error(short error, short *global);
short graf_watchbox(OBJECT *tree, short object, short in_state, short out_state);
short mt_graf_watchbox(OBJECT *tree, short object, short in_state, short out_state, short *global);
short form_button(OBJECT *tree, short object, short clicks, short *next);
short mt_form_button(OBJECT *tree, short object, short clicks, short *next, short *global);
short form_keybd(OBJECT *tree, short object, short proposed, short key, short *next, short *remaining);
short mt_form_keybd(OBJECT *tree, short object, short proposed, short key, short *next, short *remaining, short *global);
#ifndef R_TREE
#define R_TREE 0
#define R_OBJECT 1
#define R_TEDINFO 2
#define R_ICONBLK 3
#define R_BITBLK 4
#define R_STRING 5
#define R_IMAGEDATA 6
#define R_OBSPEC 7
#define R_TEPTEXT 8
#define R_TEPTMPLT 9
#define R_TEPVALID 10
#define R_IBPMASK 11
#define R_IBPDATA 12
#define R_IBPTEXT 13
#define R_BIPDATA 14
#define R_FRSTR 15
#define R_FRIMG 16
#endif
short rsrc_rcfix(void *header);
short mt_rsrc_rcfix(void *header, short *global);
short rsrc_load(const char *name);
short mt_rsrc_load(const char *name, short *global);
short rsrc_free(void);
short mt_rsrc_free(short *global);
short rsrc_gaddr(short type, short index, void *address);
short mt_rsrc_gaddr(short type, short index, void *address, short *global);
short rsrc_saddr(short type, short index, void *address);
short mt_rsrc_saddr(short type, short index, void *address, short *global);
short rsrc_obfix(OBJECT *tree, short object);
short mt_rsrc_obfix(OBJECT *tree, short object, short *global);
short form_center(OBJECT *tree, short *x, short *y, short *w, short *h);
short mt_form_center(OBJECT *tree, short *x, short *y, short *w, short *h, short *global);
short form_center_grect(OBJECT *tree, GRECT *rect);
short mt_form_center_grect(OBJECT *tree, GRECT *rect, short *global);
#define CHECK 1
#define UNCHECK 0
#define ENABLE 1
#define DISABLE 0
#define HIGHLIGHT 0
#define UNHIGHLIGHT 1
#define MENU_REMOVE 0
#define MENU_INSTALL 1
#define MN_SELECTED 10
short menu_bar(OBJECT *tree, short show);
short mt_menu_bar(OBJECT *tree, short show, short *global);
short menu_popup(MENU *menu, short x, short y, MENU *result);
short mt_menu_popup(MENU *menu, short x, short y, MENU *result, short *global);
short menu_icheck(OBJECT *tree, short item, short check);
short mt_menu_icheck(OBJECT *tree, short item, short check, short *global);
short menu_ienable(OBJECT *tree, short item, short enable);
short mt_menu_ienable(OBJECT *tree, short item, short enable, short *global);
short menu_tnormal(OBJECT *tree, short item, short normal);
short mt_menu_tnormal(OBJECT *tree, short item, short normal, short *global);
short menu_text(OBJECT *tree, short item, const char *text);
short mt_menu_text(OBJECT *tree, short item, const char *text, short *global);
short objc_sysvar(short mode, short which, short in1, short in2, short *out1, short *out2);
short mt_objc_sysvar(short mode, short which, short in1, short in2, short *out1, short *out2, short *global);
short objc_draw(OBJECT *tree, short start, short depth, short x, short y, short w, short h);
short mt_objc_draw(OBJECT *tree, short start, short depth, short x, short y, short w, short h, short *global);
short objc_change(OBJECT *tree, short object, short reserved, short x, short y, short w, short h, short state, short redraw);
short mt_objc_change(OBJECT *tree, short object, short reserved, short x, short y, short w, short h, short state, short redraw, short *global);
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
