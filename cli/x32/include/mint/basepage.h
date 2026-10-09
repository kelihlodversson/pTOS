/*
 * mint/basepage.h - the basepage pointer the x32 EmuCON is started with
 *
 * Only the field EmuCON reads is declared; its offset is the kernel PD's
 * (include/bdosdefs.h) and is checked against it at build time by
 * x32rt.c.
 */
#ifndef X32_BASEPAGE_H
#define X32_BASEPAGE_H

struct basepage {
    char pad[0x2c];
    char *p_env;                /* pointer to the environment string */
};

extern struct basepage *_base;

#endif
