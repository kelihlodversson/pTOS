/*
 * gemdosif.h - EmuTOS AES functions and variables implemented in gemdosif.S
 *
 * Copyright (C) 2002-2020 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GEMDOSIF_H
#define GEMDOSIF_H


extern PFVOID   drwaddr;

extern void *   tiksav;

extern LONG     NUM_TICK;                       /* number of ticks      */
                                                /*   since last sample  */
                                                /*   while someone was  */
                                                /*   waiting            */
extern LONG     CMP_TICK;                       /* indicates to tick    */
                                                /*   handler how much   */
                                                /*   time to wait before*/
                                                /*   sending the first  */
                                                /*   tchange            */


/*
 * far_bcha()/far_mcha(): installed as the VDI's own linea_vars.user_but/
 * user_mot vectors (aes/gemgsxif.c's gsx_setmb()), whose real calling
 * convention is "void (*)(WORD)" / "ULONG (*)(WORD,WORD)" respectively
 * (see vdi/vdi_mouse.c's default_user_mot() and vdi/arch/x86_64/
 * vdi_entry.c's/vdi/arch/arm/vdi_entry.c's mouse_int(), which call
 * through those two slots with exactly these signatures) -- declared
 * here to match on every arch, even though m68k/ARM's own gemdosif.S
 * implementations are raw assembly with no C-checked prototype of their
 * own, so that aes/arch/x86_64/gemdosif.c's real C definitions don't
 * conflict with a stale "(void)" declaration in the same translation
 * unit.
 */
extern void far_bcha(WORD state);
extern ULONG far_mcha(WORD x, WORD y);
#if CONF_WITH_EXTENDED_MOUSE
extern void aes_wheel(void);
#endif
extern void justretf(void);
/*
 * tikcod(): installed as etv_timer (bios.c's Setexc(0x100, ...)), whose
 * real calling convention is "void (*)(int)" (bios/tosvars.c's own
 * "void (*etv_timer)(int);") -- same reasoning as far_bcha()/far_mcha()
 * above.
 */
extern void tikcod(int ms);

extern void unset_aestrap(void);
extern void set_aestrap(void);

extern void takeerr(void);
extern void giveerr(void);
extern void retake(void);

extern void drawrat(WORD newx, WORD newy);

extern void aestrap(void);

#endif
