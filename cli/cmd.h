/*
 * EmuCON2 header
 *
 * Copyright (C) 2013-2017 The EmuTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef STANDALONE_CONSOLE
 #include "config.h"
 #include <nls.h>
 #include <portab.h>
 /* the ROM build shares cli/ across every machine, so gate resolution
    switching on the Atari video hardware actually configured in */
 #define CLI_WITH_RESOLUTION    CONF_WITH_ATARI_VIDEO
 #define CLI_WITH_TT_RESOLUTION CONF_WITH_TT_SHIFTER
#else
 #define _(a) a
 #define N_(a) a
 #define gettext(a) a
 typedef unsigned char   UBYTE;
 typedef short int       WORD;
 typedef unsigned short  UWORD;
 typedef long            LONG;
 typedef unsigned long   ULONG;
 #define MAXPATHLEN      256
 #define BLKDEVNUM       26
 #define LOWORD(x) ((UWORD)(ULONG)(x))
 #define HIWORD(x) ((UWORD)((ULONG)(x) >> 16))
 #define LOBYTE(x) ((UBYTE)(UWORD)(x))
 #define HIBYTE(x) ((UBYTE)((UWORD)(x) >> 8))
 /* the standalone tool only ever targets real Atari hardware */
 #define CLI_WITH_RESOLUTION    1
 #define CLI_WITH_TT_RESOLUTION 1
 /* normally from portab.h, which this build doesn't include */
 #define FALLTHROUGH do { } while (0)
#endif


/*
 * system calls
 *
 * The standalone build (emucon2.tos) is linked against libcmini, whose
 * <mint/osbind.h> already provides every GEMDOS/BIOS/XBIOS call below
 * under these exact names -- no local trap wrappers needed. The ROM
 * build uses the shared portable dispatchers that already work on both
 * m68k and ARM.
 */
#ifdef STANDALONE_CONSOLE
#include <mint/osbind.h>
#include <string.h>     /* strlen(), strcpy(), memcpy(), memset(), strncasecmp() */
#include <ctype.h>      /* toupper() */

/* EmuTOS/pTOS extends the standard 3-argument XBIOS Setscreen (opcode 5)
 * with a 4th word argument that sets the font height in the same call
 * (see xbios_v_llww() in include/xbiosbind.h, used by the ROM build
 * below); libcmini's <mint/osbind.h> only knows the standard,
 * 3-argument form. */
#undef Setscreen
#define Setscreen(lscrn,pscrn,rez,height) \
    ((void)trap_14_wllww((short)(0x05),(long)(lscrn),(long)(pscrn), \
                          (short)(rez),(short)(height)))

#else /* ROM build: use the shared portable trap dispatchers */
#include "asm.h"        /* trap1(), trap1_pexec() */
#include "biosbind.h"   /* Bconstat(), Bconin(), Bconout() */
#include "xbiosbind.h"  /* Setscreen(), Cursconf(), Kbrate() */

/* xbiosbind.h declares Supexec void (marked "??? void") but EmuCON reads
   the called function's return value.  Use xbios_l_lll with two harmless
   padding arguments so we get the long return value back.              */
#undef Supexec
static __inline__ long cli_supexec_(long a)
{
    return xbios_l_lll(38, a, 0L, 0L);
}
#define Supexec(a) cli_supexec_((long)(a))

#define jmp_gemdos_v(a)         trap1((int)(a))
#define jmp_gemdos_w(a,b)       trap1((int)(a),(WORD)(b))
#define jmp_gemdos_l(a,b)       trap1((int)(a),(LONG)(b))
#define jmp_gemdos_p(a,b)       trap1((int)(a),(void*)(b))
#define jmp_gemdos_ww(a,b,c)    trap1((int)(a),(WORD)(b),(WORD)(c))
#define jmp_gemdos_pw(a,b,c)    trap1((int)(a),(void *)(b),(WORD)(c))
#define jmp_gemdos_wlp(a,b,c,d) trap1((int)(a),(WORD)(b),(LONG)(c),(void *)(d))
#define jmp_gemdos_wpp(a,b,c,d) trap1((int)(a),(WORD)(b),(void *)(c),(void *)(d))
#define jmp_gemdos_pww(a,b,c,d) trap1((int)(a),(void *)(b),(WORD)(c),(WORD)(d))
/* Pexec needs the 5-argument form; trap1_pexec handles the extra argument */
#define jmp_gemdos_wppp(a,b,c,d,e) \
    trap1_pexec((short)(b),(const char *)(c),(const void *)(d),(const char *)(e))

#define Dsetdrv(a)          jmp_gemdos_w(0x0e,a)
#define Dgetdrv()           jmp_gemdos_v(0x19)
#define Fgetdta()           jmp_gemdos_v(0x2f)
#define Sversion()          jmp_gemdos_v(0x30)
#define Dfree(a,b)          jmp_gemdos_pw(0x36,a,b)
#define Dcreate(a)          jmp_gemdos_p(0x39,a)
#define Ddelete(a)          jmp_gemdos_p(0x3a,a)
#define Dsetpath(a)         jmp_gemdos_p(0x3b,a)
#define Fcreate(a,b)        jmp_gemdos_pw(0x3c,a,b)
#define Fopen(a,b)          jmp_gemdos_pw(0x3d,a,b)
#define Fclose(a)           jmp_gemdos_w(0x3e,a)
#define Fread(a,b,c)        jmp_gemdos_wlp(0x3f,a,b,c)
#define Fwrite(a,b,c)       jmp_gemdos_wlp(0x40,a,b,c)
#define Fdelete(a)          jmp_gemdos_p(0x41,a)
#define Fattrib(a,b,c)      jmp_gemdos_pww(0x43,a,b,c)
#define Fdup(a)             jmp_gemdos_w(0x45,a)
#define Fforce(a,b)         jmp_gemdos_ww(0x46,a,b)
#define Dgetpath(a,b)       jmp_gemdos_pw(0x47,a,b)
#define Malloc(a)           jmp_gemdos_l(0x48,a)
#define Mfree(a)            jmp_gemdos_p(0x49,a)
#define Pexec(a,b,c,d)      jmp_gemdos_wppp(0x4b,a,b,c,d)
#define Fsfirst(a,b)        jmp_gemdos_pw(0x4e,a,b)
#define Fsnext()            jmp_gemdos_v(0x4f)
#define Frename(a,b,c)      jmp_gemdos_wpp(0x56,a,b,c)

#endif /* STANDALONE_CONSOLE */


/*
 * program parameters
 */
#define CMDLINELEN      128     /* allow for length char etc */
#define MAXCMDLINE      125     /* the most amount of real data allowed */

#define IOBUFSIZE       16384L  /* buffer size */

#define MAX_LINE_SIZE   200     /* must be greater than the largest screen width */
#define HISTORY_SIZE    10      /* number of lines of history */
#define MAX_ARGS        30      /* maximum number of args we can parse */

#define LOCAL           static  /* comment out for testing */
#define PRIVATE         static  /* comment out for testing */

/*
 * date/time display format stuff
 */
#define _IDT_COOKIE     0x5f494454      /* '_IDT' */
#define _IDT_MDY        0               /* date format: month-day-year */
#define _IDT_DMY        1               /*              day-month-year */
#define _IDT_YMD        2               /*              year-month-day */
#define _IDT_YDM        3               /*              year-day-month */
#define _IDT_12H        0               /* time format: 12-hour */
#define _IDT_24H        1               /*              24-hour */

#define DEFAULT_DT_SEPARATOR    '/'
#define DEFAULT_DT_FORMAT   ((_IDT_12H<<12) + (_IDT_YMD<<8) + DEFAULT_DT_SEPARATOR)

/*
 * video stuff
 */
#define _VDO_COOKIE     0x5f56444fL     /* '_VDO' */
#define _VDO_ST         0x00000000L     /* ST */
#define _VDO_TT         0x00020000L     /* TT */
#define _VDO_VIDEL      0x00030000L     /* Falcon videl */
#define ST_LOW          0               /* from Getrez() */
#define ST_MEDIUM       1
#define ST_HIGH         2
#define TT_MEDIUM       4
#define TT_HIGH         6
#define TT_LOW          7

/*
 *  typedefs
 */
typedef struct {
    char    d_reserved[21];
    char    d_attrib;
    WORD    d_time;
    WORD    d_date;
    LONG    d_length;
    char    d_fname[14];
} DTA;

/* Type of function run by execute() */
typedef LONG FUNC(WORD argc,char **argv);

/*
 *  return codes from get_next_arg()
 */
#define ARG_NORMAL      1
#define NO_MORE_ARGS    0
#define QUOTING_ERROR   -1

/*
 *  manifest constants
 */
#define EFILNF          -33
#define EPTHNF          -34
#define ENHNDL          -35
#define EACCDN          -36
#define ENSMEM          -39
#define EDRIVE          -46
#define ENMFIL          -49
                                /* additional emucon-only error codes */
#define USER_BREAK      -100        /* user interrupted long output */
#define INVALID_PATH    -101        /* invalid component for PATH command */
#define DISK_FULL       -102
#define CMDLINE_LENGTH  -103
#define DIR_NOT_EMPTY   -104        /* translated from EACCDN for folders */
#define CANT_DELETE     -105        /* translated from EACCDN for files */
#define CHANGE_RES      -125        /* returned by mode command */
#define INVALID_PARAM   -126        /* for builtin commands */
#define WRONG_NUM_ARGS  -127        /* for builtin commands */

#define ESC             0x1b
#define DBLQUOTE        0x22

#define CTL_C           ('C'-0x40)
#define CTL_Q           ('Q'-0x40)
#define CTL_S           ('S'-0x40)

#define blank_line()    escape('l')
#define clear_screen()  escape('E')
#define cursor_left()   escape('D')
#define cursor_right()  escape('C')
#define enable_cursor() escape('e')
#define conin()         Bconin(2)
#define constat()       Bconstat(2)
#define conout(c)       Bconout(2,c)

#define LOOKUP_EXIT     (FUNC *)-1L     /* special return values from lookup_builtin() */
#define LOOKUP_ARGS     (FUNC *)-2L

/*
 *  global variables
 */
extern LONG idt_value;
extern UWORD screen_cols, screen_rows;
extern UWORD linesize;
extern WORD linewrap;
extern DTA *dta;
extern LONG redir_handle;
extern char user_path[MAXPATHLEN];     /* from PATH command */
extern WORD current_res, requested_res;

/*
 *  function prototypes
 */
/* cmdmain.c */
void outlong(ULONG n,WORD width,char filler);
int valid_res(WORD res);

/* cmdedit.c */
WORD init_cmdedit(void);
void insert_char(char *line,WORD pos,WORD len,char c);
WORD read_line(char *line);
void save_history(const char *line);
void term_cmdedit(void);

/* cmdexec.c */
LONG exec_program(WORD argc,char **argv,char *redir_name);

/* cmdint.c */
LONG (*lookup_builtin(WORD argc,char **argv))(WORD,char **);

/* cmdparse.c */
WORD parse_line(char *line,char **argv,char *redir_name);

/* cmdutil.c */
void convulong(char *buf,ULONG n,WORD width,char filler);
WORD decode_date_time(char *s,UWORD date,UWORD time);
void errmsg(LONG rc);
void escape(char c);
WORD getcookie(LONG cookie,LONG *pvalue);
WORD getword(char *buf);
WORD get_path_component(const char **pp,char *dest);
WORD has_wildcard(const char *name);
void message(const char *msg);
void messagenl(const char *msg);
const char *program_extension(const DTA *dta);
WORD strequal(const char *s1,const char *s2);
char *strlower(char *str);
char *strupper(char *str);

/* cmdasm.S (m68k) or cmdgetwh.c (ARM) */
ULONG getwh(void);
WORD getht(void);
