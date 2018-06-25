/*
 * aciavecs.S - exception handling for ikbd/midi acias.
 *
 * Copyright (C) 2001-2017 The EmuTOS development team
 *
 * Authors:
 *  LVL  Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

// (following text is taken from the Atari Compendium, xbios(0x22)
//
// Kbdvbase() returns a pointer to a system structure KBDVECS which
// is defined as follows:
//
// typedef struct
// {
//   void (*midivec)( UBYTE data );  /* MIDI Input */
//   void (*vkbderr)( UBYTE data );  /* IKBD Error */
//   void (*vmiderr)( UBYTE data );  /* MIDI Error */
//   void (*statvec)(char *buf);     /* IKBD Status */
//   void (*mousevec)(char *buf);    /* IKBD Mouse */
//   void (*clockvec)(char *buf);    /* IKBD Clock */
//   void (*joyvec)(char *buf);      /* IKBD Joystick */
//   void (*midisys)( void );        /* Main MIDI Vector */
//   void (*ikbdsys)( void );        /* Main IKBD Vector */
// } KBDVECS;
//
//- midivec is called with the received data byte in d0.
//- If an overflow error occurred on either ACIA, vkbderr or vmiderr
//  will be called, as appropriate by midisys or ikbdsys with the
//  contents of the ACIA data register in d0.
//- statvec, mousevec, clockvec, and joyvec all are called with
//  the address of the packet in register a0.
//- midisys and ikbdsys are called by the MFP ACIA interrupt handler
//  when a character is ready to be read from either the midi or
//  keyboard ports.
//
// In addition to the documented features, it was deemed necessary to add
// the following undocumented features, located in memory immediately before
// and after the documented part of the KBDVECS structure:
//
// struct UNDOCUMENTED {
//   void (*kbdvec)( UBYTE data );   /* KBD Input, TOS >= 2.0 */
//   KBDVECS kbdvecs;
//   char ikbdstate;
//   char kbdlength;
// };
//
//- kbdvec (undocumented feature of TOS >= 2.0) is called with the
//  received data byte in d0.
//- The ikbdstate description in the Compendium is wrong. It should read:
//  "When the ikbdstate variable is non-zero, it means that a packet is
//  currently being retrieved. In that case, kbdlength represents the number
//  of remaining bytes to retrieve that are part of an IKBD packet."

/* Known differences between this and Atari TOS:
 *
 * a) Although this doesn't appear to be documented, the TOS setups the
 * following registers before entering vectors ikbdsys and midisys:
 *   a0: address of the iorec structure
 *   a1: address of the ACIA control register
 *   a2: address of the error routine (vkbderr or vmiderr)
 *   d2: value of the ACIA status register
 * Currently EmuTOS does not setup these registers.
 *
 * b) In EmuTOS the same buffer kbdbuf is used to accumulate the various IKBD
 * packets, whereas TOS uses five different buffers:
 *   statvec, mousevec (absolute), mousevec (relative), clockvec, joyvec.
 *
 * c) EmuTOS currently does not check errors at all. Atari TOS checks the
 * following errors:
 *  - if an interrupt request was received and no full byte is ready to be
 *    read, then TOS jumps to the error vector;
 *  - if a byte was read but an overrun occurred, then after having
 *    correctly processed the received byte, TOS reads the data register
 *    again, then jumps to the error vector.
 */

#include "config.h"
#include "portab.h"
#include "biosext.h"
#include "bios.h"
#include "tosvars.h"
#include "lineavars.h"
#include "ikbd.h"
#include "iorec.h"
#include "vectors.h"


// ==== IOREC BUFFERS ======================================================
// Table of input-output buffers for kbd in, midi in

static UBYTE ikbdibufbuf[0x100];
static UBYTE midiibufbuf[0x80];

// ==== IORECS =============================================================
// Table of input-output records for kbd in, midi in

volatile IOREC ikbdiorec, midiiorec;


// ==== KBDVBASE =============================================================
// This is now the table of routines for managing midi and keyboard data
// in packets from IKBD

void (*mousexvec)(WORD scancode);    // Handler for executing extra mouse vectors
void (*_kbdvec)( UBYTE data );
struct kbdvecs kbdvecs;
char ikbdstate;       // action to take upon packet completion
char kbdlength;       // number of bytes remaining in current packet

WORD kbdindex;        // position of next byte in buffer
UBYTE kbdbuf[8];       // buffer where packets are being reconstructed
UBYTE joybuf[3];


void midivec( UBYTE data );
void kbdvec( UBYTE data );
static void _dummy( void ) {}

void init_acia_vecs(void)
{
    kbdvecs.mousevec = (void (*)(char *))_dummy;
    _kbdvec = kbdvec;
    kbdvecs.midivec = midivec;
    kbdvecs.vkbderr = (void (*)(UBYTE))_dummy;
    kbdvecs.vmiderr = (void (*)(UBYTE))_dummy;
    kbdvecs.statvec = (void (*)(char *))_dummy;
    kbdvecs.mousevec = (void (*)(char *))_dummy;

    ikbdiorec.buf = ikbdibufbuf;
    ikbdiorec.size = 0x100;
    ikbdiorec.head = 0;
    ikbdiorec.tail = 0;
    ikbdiorec.low = 0x40;
    ikbdiorec.high = 0xC0;

    midiiorec.buf = midiibufbuf;
    midiiorec.size = 0x80;
    midiiorec.head = 0;
    midiiorec.tail = 0;
    midiiorec.low = 0x20;
    midiiorec.high = 0x60;

    ikbdstate = 0;
    joybuf[1] = 0;
    joybuf[2] = 0;
}

void midivec( UBYTE data )
{
    // push byte data in d0 into midi iorec.
    WORD new_tail = midiiorec.tail + 1;
    if(new_tail >= midiiorec.size)
    {
        new_tail = 0;
    }
    if(new_tail != midiiorec.head)
    {
        midiiorec.buf[new_tail] = data;
        midiiorec.tail = new_tail;
    }
}

// ==== IKBD stuff ================
//
// Packets received from the IKBD are accumulated into the kbdbuf buffer.
// The packet header (F6 to FF) determines the packet length and the
// action to be taken once the packet has been received completely.
// During the reception of a packet, variable ikbdstate contains the
// action number, and variable kbdlength contains the number of bytes
// not received yet.
//
// action <--whole IKBD packet-->  Comment
// number    <-given to routine->
//
//  1     F6 a1 a2 a3 a4 a5 a6 a7 (miscellaneous, 7 bytes)
//  2     F7 0b xh xl yh yl       (absolute mouse)
//  3        F8 dx dy             (relative mouse, no button)
//  3        F9 dx dy             (relative mouse, button 1)
//  3        FA dx dy             (relative mouse, button 2)
//  3        FB dx dy             (relative mouse, both buttons)
//  4     FC yy MM dd hh mm ss    (date and time)
//  5     FD j0 j1                (both joysticks)
//  6        FE bj                (joystick 0)
//  7        FF bj                (joystick 1)
//

void kbdvec( UBYTE data )
{
    kbd_int(data);
}

#if 0 // TODO

// Process a raw byte received from IKBD protocol.
// This routine is hardware independant.
// The byte to process is in d0.l
        .globl  ikbdraw
ikbdraw:
        tst.b   ikbdstate       // inside a multi-byte packet?
        jne     in_packet       // ikbdstate != 0 => go and add to the packet
        cmp.w   #0xf6,d0        // is byte a packet header?
        jcc     begin_packet    // byte >= 0xf6 => go begin receiving a packet
        move.l  kbdvec,a0       // ordinary key byte in d0. jump in vector
        jmp     (a0)            // stack is clean: no need to jsr.

begin_packet:
        move.b  d0,kbdbuf       // put the byte at beginning of the buffer
#ifdef __mcoldfire__
        lea     kbdindex,a0
        move.w  #1,(a0)         // next position in buffer is byte number 1
        sub.l   #0xf6,d0
        lea     ikbdstate,a0
        move.b  ikbdstate_table(pc,d0),(a0)
        lea     kbdlength,a0
        move.b  kbdlength_table(pc,d0),(a0)
#else
        move.w  #1,kbdindex     // next position in buffer is byte number 1
        sub.b   #0xf6,d0
        move.b  ikbdstate_table(pc,d0),ikbdstate
        move.b  kbdlength_table(pc,d0),kbdlength
#endif
        rts
ikbdstate_table:
        .dc.b   1, 2, 3, 3, 3, 3, 4, 5, 6, 7
kbdlength_table:
        .dc.b   7, 5, 2, 2, 2, 2, 6, 2, 1, 1
        .even

in_packet:
#ifdef __mcoldfire__
        moveq   #0,d1
#endif
        move.w  kbdindex,d1
        lea     kbdbuf,a0
#ifdef __mcoldfire__
        move.b  d0,0(a0,d1.l)
        moveq   #0,d0
        move.b  kbdlength,d0
        subq.l  #1,d0
        move.b  d0,kbdlength
        jeq     got_packet
        moveq   #0,d0
        move.w  kbdindex,d0
        addq.l  #1,d0
        move.w  d0,kbdindex
#else
        move.b  d0,0(a0,d1.w)
        sub.b   #1,kbdlength
        jeq     got_packet
        addq.w  #1,kbdindex
#endif
        rts

// now I've got a full packet in buffer kbdbuf.
got_packet:
        moveq.l #0,d0
        move.b  ikbdstate,d0
#ifdef __mcoldfire__
        asl.l   #2,d0
#else
        asl.w   #2,d0
#endif
        move.l  action_table-4(pc,d0),a1
        lea     kbdbuf,a0
        jmp     (a1)
action_table:
        .dc.l   kbd_status      // 1
        .dc.l   kbd_abs_mouse   // 2
        .dc.l   kbd_rel_mouse   // 3
#ifdef CONF_WITH_IKBD_CLOCK
        .dc.l   kbd_clock       // 4
#else
        .dc.l   0               // 4
#endif
        .dc.l   kbd_joys        // 5
        .dc.l   kbd_joy0        // 6
        .dc.l   kbd_joy1        // 7
kbd_status:
        addq.l  #1,a0
        move.l  statvec,a1
        jra     kbd_jump_vec
kbd_abs_mouse:
        addq.l  #1,a0
kbd_rel_mouse:
        move.l  mousevec,a1
        jra     kbd_jump_vec
#ifdef CONF_WITH_IKBD_CLOCK
kbd_clock:
        addq.l  #1,a0
        move.l  clockvec,a1
        jra     kbd_jump_vec
#endif

// Joystick support is special. The buffer passed to routine joyvec
// will, in all cases, contain (package header, joystick 0, joystick 1)
// so for each kind of package we copy this info into a separate joybuf.

kbd_joys:
        lea     joybuf,a1
        move.b  (a0)+,(a1)+
        move.b  (a0)+,(a1)+
        move.b  (a0),(a1)
        subq.l  #2,a1
        jra     joy_next
kbd_joy0:
        lea     joybuf,a1
        move.b  (a0)+,(a1)
        move.b  (a0),1(a1)
        jra     joy_next
kbd_joy1:
        lea     joybuf,a1
        move.b  (a0)+,(a1)
        move.b  (a0),2(a1)
joy_next:
        move.l  a1,a0
        move.l  joyvec,a1

kbd_jump_vec:
        move.l  a0,-(sp)
        jsr     (a1)
        addq.l  #4,sp
        clr.b   ikbdstate
        rts

/******************************************************************************/
/* Call mousevec from C                                                       */
/******************************************************************************/

_call_mousevec:
        move.l  4(sp),a0
        move.l  mousevec,a1
#ifdef __mcoldfire__
        lea     -44(sp),sp
        movem.l d2-d7/a2-a6,(sp)
#else
        movem.l d2-d7/a2-a6,-(sp)
#endif
        jsr     (a1)
#ifdef __mcoldfire__
        movem.l (sp),d2-d7/a2-a6
        lea     44(sp),sp
#else
        movem.l (sp)+,d2-d7/a2-a6
#endif
        rts

#if CONF_WITH_FLEXCAN || CONF_SERIAL_IKBD

/******************************************************************************/
/* Call ikbdraw from C                                                        */
/******************************************************************************/

        .globl  _call_ikbdraw
_call_ikbdraw:
        moveq   #0,d0
        move.b  5(sp),d0                // raw IKBD byte
        jra     ikbdraw

#endif /* CONF_WITH_FLEXCAN || CONF_SERIAL_IKBD */

#ifdef MACHINE_AMIGA

/******************************************************************************/
/* Call joyvec from C                                                         */
/******************************************************************************/

        .globl  _call_joyvec
_call_joyvec:
        move.l  4(sp),a0
        move.l  joyvec,a1
#ifdef __mcoldfire__
        lea     -44(sp),sp
        movem.l d2-d7/a2-a6,(sp)
#else
        movem.l d2-d7/a2-a6,-(sp)
#endif
        jsr     (a1)
#ifdef __mcoldfire__
        movem.l (sp),d2-d7/a2-a6
        lea     44(sp),sp
#else
        movem.l (sp)+,d2-d7/a2-a6
#endif
        rts

#endif /* MACHINE_AMIGA */

#endif
