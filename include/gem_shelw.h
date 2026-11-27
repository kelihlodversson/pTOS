/* Standard shel_write() accessory selection and extended request. */
#ifndef GEM_SHELW_H
#define GEM_SHELW_H

#define SW_DEFDIR 0x0400
#define SWM_LAUNCHACC 3
#define CL_NORMAL 0
#define GEMAPP 1

typedef struct {
    char *newcmd;
    long psetlimit, prenice;
    char *defdir, *env;
    short uid, gid;
} SHELW;

#endif
