/*
 * bdosmain.c - GEMDOS main function dispatcher
 *
 * Copyright (C) 2001 Lineo, Inc.
 *               2002-2024 The EmuTOS development team
 *
 * Authors:
 *  EWF  Eric W. Fleischman
 *  JSL  Jason S. Loveman
 *  SCC  Steven C. Cavender
 *  LTG  Louis T. Garavaglia
 *  KTB  Karl T. Braun (kral)
 *  ACH  Anthony C. Hay (DR UK)
 *  MAD  Martin Doering
 *  THH  Thomas Huth
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "asm.h"
#include "fs.h"
#include "biosdefs.h"
#include "mem.h"
#include "proc.h"
#include "console.h"
#include "time.h"
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "kprint.h"
#include "ssystem.h"
#include "bdosstub.h"
#include "tosvars.h"

/*
**  externals
*/

/*
 * in rwa.S
 */

void enter(void);       /* defined in rwa.S */
void bdos_trap2(void);  /* defined in rwa.S */
PFVOID old_trap2; /* Old trap #2 handler, also used by rwa.S */

/*
 *  prototypes / forward declarations
 */

static long ni(void);
static long xgetver(void);


/*
 *  defines for some standard GEMDOS calls
 */
#define GEMDOS_FCREATE  0x3c
#define GEMDOS_FOPEN    0x3d
#define GEMDOS_FREAD    0x3f
#define GEMDOS_FWRITE   0x40


/*
 *  MiNT-compatible EOF indicator for character
 *  devices redirected to files
 */
#define EOF_INDICATOR   0x0000ff1aL


/*
 * the basepage for the initial process
 *
 * this used to be obtained via MGET, but that was a bit pointless,
 * since it was never freed
 */
static PD initial_basepage;

/* initial environment string */
static const char double_nul[2] __attribute__ ((aligned (2))) = { 0, 0 };


/*
 * SPECNAME - special name descriptor
 *
 * Each entry in the special name table (below) contains a special
 * name with the corresponding handle
 */
typedef struct {
    char *name;
    long handle;
} SPECNAME;


/*
 * table of special names, used by Fopen()/Fcreate() to access
 * a character device.  note that special names can be upper or
 * lower case, but NOT mixed case.
 */
static const SPECNAME specname_table[] =
{
    { "CON:", 0x0000ffffL },
    { "con:", 0x0000ffffL },
    { "AUX:", 0x0000fffeL },
    { "aux:", 0x0000fffeL },
    { "PRN:", 0x0000fffdL },
    { "prn:", 0x0000fffdL },
};
#define SN_ENTRIES  ARRAY_SIZE(specname_table)


/*
 * FND - Function Descriptor
 *
 * Each entry in the function table (below) consists of the address of
 * the function which corresponds to the function number, and a function
 * type.
 */
typedef struct
{
#ifdef __arm__
    union {
        long  (*p0)(void);
        long  (*p1)(long);
        long  (*p2)(long, long);
        long  (*p3)(long, long, long);
        long  (*p4)(long, long, long, long);
    } fncall;
    UBYTE stdio_typ;    /* Standard I/O channel (highest bit must be set, too) */
    UBYTE nparms;       /* Number of parameters */
#else
    /*
     * Shapes name each real parameter's width in order: 'w' a 16-bit
     * word, 'l' a 32-bit long or pointer -- matching the trap1_v()/
     * trap1_w()/... naming in include/arch/m68k/asm.h. Calling any of
     * these through a cast from a function whose *declared* parameter
     * types merely happen to share the same shape (e.g. WORD vs plain
     * int, both 4 bytes wide once promoted -- see below) is safe: GCC
     * gives every non-long stack parameter a full 4-byte slot on this
     * target once int is 32 bits, so only the long/short *shape*
     * matters, not the exact declared type.
     *
     * Before #300 removed -mshort for the kernel m68k build, plain int
     * was 16 bits, matching WORD exactly, and no stack parameter -- of
     * any type -- was ever padded: total byte count alone determined
     * every argument's stack offset, so shape didn't matter either,
     * only wparms (the raw word count) did. That is what the dispatch
     * below used to switch on, calling through homogeneous-short union
     * members (www, wwww, ...) regardless of which "words" were really
     * one WORD argument or one half of a LONG one. Once int widens to
     * 32 bits, GCC pads *only* the short/WORD slots (a real LONG
     * parameter needs no padding, already being 4 bytes), so a WORD
     * ahead of a LONG argument and a LONG ahead of a WORD argument stop
     * being interchangeable, and wparms alone can no longer select a
     * correct union member -- e.g. wparms=2 covers both xforce(WORD,
     * WORD) and xconws(a single LONG pointer). Shape says which.
     */
    union {
        long  (*v)(void);
        long  (*w)(short);
        long  (*l)(long);
        long  (*ww)(short, short);
        long  (*ll)(long, long);
        long  (*lw)(long, short);
        long  (*lww)(long, short, short);
        /*
         * xexec(WORD, char*, char*, char*) is the sole WLLL-shaped
         * function, and all three "L" slots are real pointers (path,
         * tail, env) -- not scalar longs. Under the plain stack ABI
         * that made no difference (every argument gets an identically
         * positioned 4-byte stack slot regardless of type), but under
         * -mfastcall pointer- and long-typed arguments go to different
         * register classes (a0/a1 vs d1/d2), so calling through a
         * long-typed union member here would send xexec's path/tail
         * pointers to the wrong registers. Use void* to match its real
         * signature; harmless on the plain ABI since void* and long
         * share the same stack layout there.
         */
        long  (*wlll)(short, void*, void*, void*);
        /*
         * The "L" letter in every shape above names a wire slot's
         * *width* (32 bits), not its C type -- but under -mfastcall a
         * pointer-typed argument and a scalar long-typed argument in
         * that same slot go to different register classes (a0/a1 vs.
         * d0-d2), unlike the plain stack ABI where both share the same
         * 4-byte stack layout regardless of type (see the wlll comment
         * above). Every shape below is the pointer-carrying twin of an
         * existing long-shape, added because at least one function
         * using that wire shape has a real pointer in an "L" slot;
         * calling it through the long-typed union member would send
         * that pointer to a data register instead of an address one.
         */
        long  (*p)(void*);
        long  (*pl)(void*, long);
        long  (*pw)(void*, short);
        long  (*pww)(void*, short, short);
        long  (*wlp)(short, long, void*);
        long  (*wpl)(short, void*, long);
        long  (*wpp)(short, void*, void*);
        /*
         * Same mismatch as above, but on the *return* side: xmalloc(),
         * srealloc() and xmxalloc() all return void* (a block address),
         * not a scalar long, even though their arguments are plain
         * longs/words. Under -mfastcall a function's return value comes
         * back in a0 when its own declared return type is a pointer, but
         * in d0 when it is a scalar long/int -- unlike the plain stack
         * ABI, where every GEMDOS return value is read from d0 regardless
         * of C type. Calling through a long-returning union member (l,
         * lw, ...) would read the stale d0 left over from whatever ffit()
         * or shrinkit() last did internally, instead of the a0 the callee
         * actually returned its result in. rl/rlw are the pointer-
         * returning twins of l/lw, used only for the functions whose real
         * C signature returns void* -- xtermres, the other LW-shaped
         * function, genuinely returns a scalar long and still uses lw.
         */
        void  *(*rl)(long);
        void  *(*rlw)(long, short);
    } fncall;
    UBYTE stdio_typ;    /* Standard I/O channel (highest bit must be set, too) */
    UBYTE shape;        /* FSHAPE_* -- which fncall union member to use */
#endif
} FND;

#ifndef __arm__
enum {
    FSHAPE_V, FSHAPE_W, FSHAPE_L, FSHAPE_WW, FSHAPE_LL,
    FSHAPE_LW, FSHAPE_LWW, FSHAPE_WLLL,
    FSHAPE_P, FSHAPE_PL, FSHAPE_PW, FSHAPE_PWW,
    FSHAPE_WLP, FSHAPE_WPL, FSHAPE_WPP,
    FSHAPE_RL, FSHAPE_RLW
};
#endif


/*
 * funcs - table of os functions, indexed by function number
 *
 * Each entry is for an FND structure. NI is used
 * as the address for functions not implemented.
 */

static const FND funcs[] =
{
#define F(x) { (PFLONG)(x) }
#define NI F(ni)
#ifdef __arm__
#   define W_N(n, shape) (n)
#else
#   define W_N(n, shape) FSHAPE_##shape
#endif

     { F(x0term), 0, W_N(0,V) },       /* 0x00 */

    /*
     * console functions
     *
     * on these functions, the 0x80 flag indicates std file used
     * 0x80 is std in, 0x81 is stdout, 0x82 is stdaux, 0x83 stdprn
     */

    { F(xconin),   0x80, W_N(0,V) },   /* 0x01 */
    { F(xconout),  0x81, W_N(1,W) },   /* 0x02 */
    { F(xauxin),   0x82, W_N(0,V) },   /* 0x03 */
    { F(xauxout),  0x82, W_N(1,W) },   /* 0x04 */
    { F(xprtout),  0x83, W_N(1,W) },   /* 0x05 */
    { F(xrawio),   0,    W_N(1,W) },   /* 0x06 */
    { F(xrawcin),  0x80, W_N(0,V) },   /* 0x07 */
    { F(xnecin),   0x80, W_N(0,V) },   /* 0x08 */
    { F(xconws),   0x81, W_N(1,P) },   /* 0x09 */
    { F(xconrs),   0x80, W_N(1,P) },   /* 0x0A */
    { F(xconstat), 0x80, W_N(0,V) },   /* 0x0B */

    /*
     * disk functions
     *
     * on these functions the 0x80 flag indicates whether a handle
     * is required, the low bits represent the parameter ordering,
     * as usual.
     */

    { NI, 0, 0 },
    { NI, 0, 0 },

    { F(xsetdrv),  0, W_N(1,W) },      /* 0x0E */

    { NI, 0, 0 },

    /*
     * extended console functions
     *
     * Here the 0x80 flag indicates std file used, as above
     */

    { F(xconostat), 0x81, W_N(0,V) },  /* 0x10 */
    { F(xprtostat), 0x83, W_N(0,V) },  /* 0x11 */
    { F(xauxistat), 0x82, W_N(0,V) },  /* 0x12 */
    { F(xauxostat), 0x82, W_N(0,V) },  /* 0x13 */

#if CONF_WITH_ALT_RAM
    { F(xmaddalt),  0, W_N(2,PL) },    /* 0x14 */
#else
    { NI, 0, 0 },               /* 0x14 */
#endif

#if CONF_WITH_VIDEL
    { F(srealloc),  0, W_N(1,RL) },    /* 0x15 */
#else
    { NI, 0, 0 },               /* 0x15 */
#endif

    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },

    { F(xgetdrv),  0, W_N(0,V) },      /* 0x19 */
    { F(xsetdta),  0, W_N(1,P) },      /* 0x1A */

    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },

    /* xgsps */

    { NI, 0, 0 },               /* 0x20 */
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },

    { F(xgetdate), 0, W_N(0,V) },      /* 0x2A */
    { F(xsetdate), 0, W_N(1,W) },      /* 0x2B */
    { F(xgettime), 0, W_N(0,V) },      /* 0x2C */
    { F(xsettime), 0, W_N(1,W) },      /* 0x2D */

    { NI, 0, 0 },

    { F(xgetdta),  0, W_N(0,V) },      /* 0x2F */
    { F(xgetver),  0, W_N(0,V) },      /* 0x30 */
    { F(xtermres), 0, W_N(2,LW) },     /* 0x31 */

    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },

    { F(xgetfree), 0, W_N(2,PW) },     /* 0x36 */

    { NI, 0, 0 },
    { NI, 0, 0 },

    { F(xmkdir),   0, W_N(1,P) },      /* 0x39 */
    { F(xrmdir),   0, W_N(1,P) },      /* 0x3A */
    { F(xchdir),   0, W_N(1,P) },      /* 0x3B */
    { F(xcreat),   0, W_N(2,PW) },     /* 0x3C */
    { F(xopen),    0, W_N(2,PW) },     /* 0x3D */
    { F(xclose),   0, W_N(1,W) },      /* 0x3E - will handle its own redirection */
    { F(xread),    0x82, W_N(3,WLP) }, /* 0x3F */
    { F(xwrite),   0x82, W_N(3,WLP) }, /* 0x40 */
    { F(xunlink),  0, W_N(1,P) },      /* 0x41 */
    { F(xlseek),   0x81, W_N(3,LWW) }, /* 0x42 */
    { F(xchmod),   0, W_N(3,PWW) },    /* 0x43 */
    { F(xmxalloc), 0, W_N(2,RLW) },    /* 0x44 */
    { F(xdup),     0, W_N(1,W) },      /* 0x45 */
    { F(xforce),   0, W_N(2,WW) },     /* 0x46 */
    { F(xgetdir),  0, W_N(2,PW) },     /* 0x47 */
    { F(xmalloc),  0, W_N(1,RL) },     /* 0x48 */
    { F(xmfree),   0, W_N(1,P) },      /* 0x49 */
    { F(xsetblk),  0, W_N(3,WPL) },    /* 0x4A */
    { F(xexec),    0, W_N(4,WLLL) },   /* 0x4B */
    { F(xterm),    0, W_N(1,W) },      /* 0x4C */

    { NI, 0, 0 },

    { F(xsfirst),  0, W_N(2,PW) },     /* 0x4E */
    { F(xsnext),   0, W_N(0,V) },      /* 0x4F */

    { NI, 0, 0 },               /* 0x50 */
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },
    { NI, 0, 0 },

    { F(xrename),  0, W_N(3,WPP) },    /* 0x56 */
    { F(xgsdtof),  0, W_N(3,PWW) }     /* 0x57 */
#undef F
#undef NI
#undef W_N
};
#define MAX_FNCALL (ARRAY_SIZE(funcs) - 1)


/*
 *  xgetver -
 *      return current version number
 */
static long xgetver(void)
{
    return (long)GEMDOS_VERSION;
}


/*
 *  ni -
 */
static long ni(void)
{
    return EINVFN;
}


/*
 *  osinit - the bios calls this routine to initialize the os
 */

void osinit_before_xmaddalt(void)
{
    /* take over the handling of TRAP #1 */
    Setexc(0x21, (long)enter);

    /*
     * intercept TRAP #2 only for xterm(), keeping the old value
     * so that our trap handler can call the old one
     */
    old_trap2 = (PFVOID) Setexc(0x22, (long)bdos_trap2);

    bufl_init();    /* initialize BDOS buffer list */

    osmem_init();
    umem_init();
}

/* BIOS may call xmaddalt() between those two calls */

void osinit_after_xmaddalt(void)
{
    /* Set up initial process. Required by Malloc() */
    run = &initial_basepage;
    run->p_flags = PF_STANDARD;
    run->p_env = CONST_CAST(char *,double_nul);

    time_init();

    KDEBUG(("BDOS: address of basepage = %p\n", run));

    stdhdl_init();  /* set up system initial standard handles */

    KDEBUG(("BDOS: cinit - osinit successful ...\n"));
}


/*
 *  freetree -  free the directory node tree
 */
static void freetree(DND *d)
{
    DIRTBL_ENTRY *p;
    int i;

    if (d->d_left)
        freetree(d->d_left);
    if (d->d_right)
        freetree(d->d_right);
    if (d->d_ofd)
    {
        xmfreblk(d->d_ofd);
    }
    for (i = 1, p = dirtbl+1; i < NCURDIR; i++, p++)
    {
        if (p->dnd == d)
        {
            p->dnd = NULL;
            p->use = 0;
        }
    }
    xmfreblk(d);
}


/*
 *  offree - free up all handles associated with the specified DMD
 *
 *  this is used when media change is detected on a device, in order
 *  to cause subsequent I/Os to that device for those handles to fail
 */
static void offree(DMD *d)
{
    int i;
    OFD *f;

    for (i = 0; i < OPNFILES; i++)
    {
        if (((long) (f = sft[i].f_ofd)) > 0L)
        {
            if (f->o_dmd == d)
            {
                xmfreblk(f);
                sft[i].f_ofd = NULL;
                sft[i].f_own = NULL;
                sft[i].f_use = 0;
            }
        }
    }
}


/*
 * mark_bcbs_invalid - mark the BCBs for the specified drive as invalid
 */
static void mark_bcbs_invalid(int drv)
{
    BCB *bx;
    int i;

    for (i = 0; i < 2; i++)
    {
        for (bx = bufl[i]; bx; bx = bx->b_link)
        {
            if (bx->b_bufdrv == drv)
                bx->b_bufdrv = -1;
        }
    }
}


#ifdef __arm__
long osif(LONG *pw);
#else
long osif(short *pw);
#endif

/*
 *  osif - C implementation of trap #1. Called by _enter.
 */
#ifdef __arm__
long osif(LONG *pw)
#else
long osif(short *pw)
#endif
{
    char **pb, *pb2, *p, ctmp;
    BPB *b;
    DMD *dmd;
    DND *dn;
    int typ, h, i, fn;
    int num, max;
    long rc, numl;
    const FND *f;

restrt:
    fn = pw[0];

#ifdef __arm__
    /*
     * Ssystem() (0x154) is far outside the funcs[] table above, and
     * unlike every other call handled through it, its arguments don't
     * follow the table's implicit stdio/handle conventions -- so it's
     * special-cased here instead of getting its own funcs[] slot.
     * ARM only: real m68k TOS software already has Supexec() and direct
     * memory access for this, and the smallest m68k ROM images (see
     * release.mk) have no code size to spare for a second way to do it.
     */
    if (fn == GEMDOS_SSYSTEM)
        return xssystem((WORD)pw[1], pw[2], pw[3]);
#endif

    if (fn > MAX_FNCALL)
        return EINVFN;

    KDEBUG(("BDOS (fn=0x%04x)\n",fn));

    if (setjmp(errbuf))
    {
        rc = errcode;
        /* hard error processing */
        KDEBUG(("Error code gotten from some longjmp(), back in osif(): %ld\n",rc));

        /* is this a media change ? */
        if (rc == E_CHNG)
        {
            /* first, out with the old stuff */
            dmd = drvtbl[errdrv];
            dn = dmd->m_dtl;
            offree(dmd);
            xmfreblk(dmd);
            drvtbl[errdrv] = NULL;

            if (dn)
                freetree(dn);

            mark_bcbs_invalid(errdrv);

            /* then, in with the new */
            b = (BPB *)Getbpb(errdrv);
            if (!b)
            {
                drvsel &= ~(1L<<errdrv);
                return rc;
            }

            if (log_media(b,errdrv))
            {
                drvsel &= ~(1L<<errdrv);
                return ENSMEM;
            }

            rwerr = 0;
            errdrv = 0;
            goto restrt;
        }

        /* else handle as hard error on disk for now */
        mark_bcbs_invalid(errdrv);

        return rc;
    }

    f = &funcs[fn];
    typ = f->stdio_typ;

    if (typ && fn && ((fn<12) || ((fn>=16) && (fn<=19)))) /* std funcs */
    {
        h = run->p_uft[typ & 0x7f];
        if (h > 0)
        {   /* handle standard device functions redirected to a file */
            switch(fn)
            {
            case 6:                 /* Crawio() */
                if (pw[1] != 0xFF)
                    goto rawout;
                FALLTHROUGH;
            case 1:                 /* Cconin() */
            case 3:                 /* Cauxin() */
            case 7:                 /* Crawcin() */
            case 8:                 /* Cnecin() */
                /*
                 * if an error occurs when reading the file, we handle it
                 * the same way as MiNT (standard TOS returns garbage here)
                 */
                if (xread(h,1L,&ctmp) != 1L)
                    return EOF_INDICATOR;
                return ctmp;

            case 2:                 /* Cconout */
            case 4:                 /* Cauxout() */
            case 5:                 /* Cprnout() */
                /*  M01.01.07  */
                /*  write the char in the int at pw[1]  */
            rawout:
                xwrite(h , 1L , ((char*) &pw[1])+1);
                return 0; /* dummy */

            case 9:                 /* Cconws() */
                pb2 = *((char **) &pw[1]);
                xwrite(h,strlen(pb2),pb2);
                return 0; /* dummy */

            case 10:                /* Cconrs() */
                pb2 = *((char **) &pw[1]);
                max = *pb2++;
                p = pb2 + 1;
                for (i = 0; max--; i++, p++)
                {
                    if (xread(h,1L,p) == 1)
                    {
                        if (*p == 0x0d)
                        {       /* eat the lf */
                            xread(h,1L,&ctmp);
                            break;
                        }
                    }
                    else
                        break;
                }
                *pb2 = i;
                return 0;

            case 11:                /* Cconis() */
            case 18:                /* Cauxis() */
                if (eof(h))
                    return 0L;
                FALLTHROUGH;

            case 16:                /* Cconos() */
            case 17:                /* Cprnos() */
            case 19:                /* Cauxos() */
                return -1L;
            }
        }

        typ = 0;
    }

    if (typ & 0x80)
    {
        if (typ == 0x81)
            h = pw[3];
        else
            h = pw[1];

        if (h >= NUMSTD)
        {
            numl = (long) sft[h-NUMSTD].f_ofd;
#if CONF_WITH_PLUGGABLE_FS
            if (!numl)
                numl = (long) sft[h-NUMSTD].f_pfs.fs;
#endif
        }
        else if (h >= 0)
        {
            h = run->p_uft[h];
            if (h > 0)
            {
                numl = (long) sft[h-NUMSTD].f_ofd;
#if CONF_WITH_PLUGGABLE_FS
                if (!numl)
                    numl = (long) sft[h-NUMSTD].f_pfs.fs;
#endif
            }
            else
                numl = h;
        }
        else
            numl = h;

        if (!numl)
            return EIHNDL;  /* invalid handle: media change, etc */

        if (numl < 0)
        {       /* prn, aux, con */
                /* -3   -2   -1  */

            num = numl;

            /*  check for valid handle  */ /* M01.01.0528.01 */
            if (num < -3)
                return EIHNDL;

            pb = (char **) &pw[4];

            /* only do things on read and write */

            if (fn == GEMDOS_FREAD)     /* read */
            {
                if (pw[2])              /* disallow HUGE reads      */
                    return 0;

                if (pw[3] == 1)
                {
                    **pb = conin(HXFORM(num));
                    return 1;
                }

                return cgets(HXFORM(num),pw[3],*pb);
            }

            if (fn == GEMDOS_FWRITE)    /* write */
            {
                long n, count = *(long *)&pw[2];

                pb2 = *pb;      /* char * is buffer address */

                for (n = 0; n < count; n++)
                {
                    if (num == H_Console)
                        tabout(HXFORM(num), (unsigned char)*pb2++);
                    else
                    {           /* M01.01.1029.01 */
                        if (Bconout(HXFORM(num), (unsigned char)*pb2++) == 0)
                            return n;
                    }
                }

                return count;
            }

            return 0;
        }
    }


    /*
     * for Fopen(), Fcreate() we check for special names
     */
    rc = 0;
    if ((fn == GEMDOS_FOPEN) || (fn == GEMDOS_FCREATE)) /* open, create */
    {
        const SPECNAME *entry;
        p = *((char **) &pw[1]);
        for (i = 0, entry = specname_table; i < SN_ENTRIES; i++, entry++)
        {
            if (strcmp(p,entry->name) == 0)
            {
                rc = entry->handle;
                break;
            }
        }
    }

    if (!rc)
    {
#ifdef __arm__
        switch(f->nparms)
        {
        case 0:
            rc = (*f->fncall.p0)();
            break;

        case 1:
            rc = (*f->fncall.p1)(pw[1]);
            break;

        case 2:
            rc = (*f->fncall.p2)(pw[1],pw[2]);
            break;

        case 3:
            rc = (*f->fncall.p3)(pw[1],pw[2],pw[3]);
            break;

        case 4:
            rc = (*f->fncall.p4)(pw[1],pw[2],pw[3],pw[4]);
            break;
        default:
            rc = EINTRN;    /* Internal error */
        }
#else
        /*
         * A LONG argument spans two consecutive words in pw[]; pwlong()
         * reassembles it from those two big-endian WORDs by value
         * (shift+or), not by reinterpreting pw's storage as a LONG --
         * that would alias a WORD lvalue through a LONG pointer, which
         * is undefined behaviour under strict aliasing even though
         * m68k itself never faults on a word-aligned-but-not-longword-
         * aligned access.
         */
#define PWLONG(i) pwlong(pw, i)
        switch(f->shape)
        {
        case FSHAPE_V:
            rc = (*f->fncall.v)();
            break;

        case FSHAPE_W:
            rc = (*f->fncall.w)(pw[1]);
            break;

        case FSHAPE_L:
            rc = (*f->fncall.l)(PWLONG(1));
            break;

        case FSHAPE_WW:
            rc = (*f->fncall.ww)(pw[1],pw[2]);
            break;

        case FSHAPE_LL:
            rc = (*f->fncall.ll)(PWLONG(1),PWLONG(3));
            break;

        case FSHAPE_LW:
            rc = (*f->fncall.lw)(PWLONG(1),pw[3]);
            break;

        case FSHAPE_LWW:
            rc = (*f->fncall.lww)(PWLONG(1),pw[3],pw[4]);
            break;

        case FSHAPE_WLLL:
            rc = (*f->fncall.wlll)(pw[1],(void*)PWLONG(2),(void*)PWLONG(4),(void*)PWLONG(6));
            break;

        case FSHAPE_P:
            rc = (*f->fncall.p)((void*)PWLONG(1));
            break;

        case FSHAPE_PL:
            rc = (*f->fncall.pl)((void*)PWLONG(1),PWLONG(3));
            break;

        case FSHAPE_PW:
            rc = (*f->fncall.pw)((void*)PWLONG(1),pw[3]);
            break;

        case FSHAPE_PWW:
            rc = (*f->fncall.pww)((void*)PWLONG(1),pw[3],pw[4]);
            break;

        case FSHAPE_WLP:
            rc = (*f->fncall.wlp)(pw[1],PWLONG(2),(void*)PWLONG(4));
            break;

        case FSHAPE_WPL:
            rc = (*f->fncall.wpl)(pw[1],(void*)PWLONG(2),PWLONG(4));
            break;

        case FSHAPE_WPP:
            rc = (*f->fncall.wpp)(pw[1],(void*)PWLONG(2),(void*)PWLONG(4));
            break;

        case FSHAPE_RL:
            rc = (long)(*f->fncall.rl)(PWLONG(1));
            break;

        case FSHAPE_RLW:
            rc = (long)(*f->fncall.rlw)(PWLONG(1),pw[3]);
            break;

        default:
            rc = EINTRN;    /* Internal error */
        }
#undef PWLONG
#endif
    }

    KDEBUG(("BDOS returns: 0x%08lx\n",rc));

    return rc;
}
