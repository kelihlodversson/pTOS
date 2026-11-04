/* Standard extended shel_write() request. */
#ifndef GEM_SHELW_H
#define GEM_SHELW_H

#define SW_DEFDIR 0x0400

typedef struct {
    char *newcmd;
    long psetlimit, prenice;
    char *defdir, *env;
    short uid, gid;
} SHELW;

#endif
