/*
 * arm_acc_probe.c - opt-in ARM desk accessory startup probe
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <mint/osbind.h>

extern short _app;

typedef unsigned short UWORD;

typedef struct {
    UWORD *control;
    UWORD *global;
    UWORD *intin;
    UWORD *intout;
    long *addrin;
    long *addrout;
} GEMBLK;

static UWORD control[5];
static UWORD global[15];
static UWORD intin[16];
static UWORD intout[7];
static UWORD message[8];
static long addrin[3];
static long addrout[1];
static GEMBLK gemblk;

static void aes_call(void)
{
    register UWORD opcode __asm__("r0") = 200;
    register GEMBLK *params __asm__("r1") = &gemblk;

    __asm__ volatile (
        "svc 2"
        : "+r" (opcode), "+r" (params)
        :
        : "r2", "r3", "r7", "r12", "lr", "memory", "cc");
}

static int valid_basepage(void)
{
    unsigned long base;
    unsigned long text;
    unsigned long end;

    if (!_base || _base->p_lowtpa != (char *)_base
        || !_base->p_tbase || _base->p_tlen <= 0 || !_base->p_hitpa)
        return 0;
    base = (unsigned long)_base;
    text = (unsigned long)_base->p_tbase;
    end = (unsigned long)_base->p_hitpa;
    if (text < base || text >= end)
        return 0;
    return (unsigned long)_base->p_tlen <= end - text;
}

int main(void)
{
    unsigned long sp;

    __asm__ volatile ("mov %0, sp" : "=r" (sp));
    if (_app || !valid_basepage() || (sp & 7)) {
        (void)Cconws("arm-acc-probe: invalid accessory entry\r\n");
        return 1;
    }

    gemblk.control = control;
    gemblk.global = global;
    gemblk.intin = intin;
    gemblk.intout = intout;
    gemblk.addrin = addrin;
    gemblk.addrout = addrout;

    control[0] = 10;             /* appl_init */
    control[1] = 0;
    control[2] = 1;
    control[3] = 0;
    aes_call();
    (void)Cconws("arm-acc-probe: appl_init\r\n");

    control[0] = 12;             /* appl_write */
    control[1] = 2;
    control[2] = 1;
    control[3] = 1;
    intin[0] = global[2];        /* send to this application's queue */
    intin[1] = 16;
    message[0] = 0x7fff;         /* probe message */
    addrin[0] = (long)message;
    aes_call();

    control[0] = 23;             /* evnt_mesag */
    control[1] = 0;
    control[2] = 1;
    control[3] = 1;
    addrin[0] = (long)message;
    aes_call();
    (void)Cconws("arm-acc-probe: message\r\n");

    control[0] = 19;             /* appl_exit */
    control[1] = 0;
    control[2] = 1;
    control[3] = 0;
    aes_call();
    return 0;
}
