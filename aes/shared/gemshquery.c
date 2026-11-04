/* Native AES startup, environment and file lookup; shared with libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "string.h"
#include "obdefs.h"
#include "miscutil.h"
#include "optimize.h"
#ifdef AES_SHELL_BINDINGS
#include AES_SHELL_BINDINGS
#else
#include "struct.h"
#include "aesvars.h"
#include "gemlib.h"
#include "geminit.h"
#include "gemdosif.h"
#include "gemdos.h"
#include "gempd.h"
#include "gemshlib.h"
#define SH_COMMAND D.s_cmd
#define SH_TAIL ad_stail
#define SH_ENVIRONMENT ad_envrn
#define SH_ENV_ENTRY(p) TRUE
#define SH_APPDIR rlr->p_appdir
#define SH_WORK D.g_work
#define SH_DTA D.g_dta
#define SH_WORK_BYTES sizeof(D.g_work)
#define SH_COPY(dst,src,size) (strcpy(dst,src),TRUE)
#define SH_APPEND(dst,src,size) (strcat(dst,src),TRUE)
#define SH_PATH_VALID(path,name) TRUE
#define SH_PATH_NEXT(end) ((end)+1)
#define SH_EMPTY_PATH(path) ((path)++)
#endif
#include "gemshquery.h"

void sh_read(char *pcmd, char *ptail)
{
    strcpy(pcmd, SH_COMMAND);
    memcpy(ptail, SH_TAIL, PDCLSIZE);
}


/*
 *  Return a pointer to the start of the filename in a path
 *  (assumed to be the last component of the path)
 */
char *sh_name(char *ppath)
{
    char *pname = ppath;

    /*
     * note: filename_start() assumes that there is a filename separator
     * within the path, so we handle a path like X:AAAAAAAA.BBB before
     * calling the general function
     */
    if (extract_drive_number(pname) >= 0)   /* valid prefix exists */
        pname += 2;

    return filename_start(pname);
}


/*
 *  Search for a particular string in the DOS environment and return a
 *  value in the pointer pointed to by the first argument.  If the string
 *  is found, the value is a pointer to the first character after the
 *  string; otherwise it is a NULL pointer.
 */
void sh_envrn(char **ppath, const char *psrch)
{
    char *p;
    WORD len;

    len = strlen(psrch);
    *ppath = NULL;

    /*
     * scan environment string until double nul
     */
    for (p = SH_ENVIRONMENT; *p && SH_ENV_ENTRY(p); )
    {
        if (strncmp(p, psrch, len) == 0)
        {
            *ppath = p + len;
            break;
        }
        while(*p++) /* skip to end of current env variable */
            ;
    }
}


/*
 *  Search next style routine to pick up each path in the PATH= portion
 *  of the DOS environment.  It returns a pointer to the start of the
 *  following path until there are no more paths to find.
 */
static char *sh_path(char *src, char *dest, char *pname)
{
    char last = 0;
    char *p;

    if (!src)           /* precautionary */
        return NULL;

    /* check for end of PATH= env var */
    if (!*src)
        return NULL;

    /* The bounded userspace workspace may skip an overlong PATH component. */
    if (!SH_PATH_VALID(src,pname)) {
        *dest = 0;
        for (p = src; *p && *p != ';' && *p != ','; p++) ;
        return SH_PATH_NEXT(p);
    }

    /* copy over path */
    for (p = src; *p; )
    {
        if ((*p == ';') || (*p == ','))
            break;
        last = *p;
        *dest++ = *p++;
    }

    /* see if extra path separator is needed */
    if ((last != PATHSEP) && (last != DRIVESEP))
        *dest++ = PATHSEP;

    /* append file name */
    strcpy(dest, pname);

    /* point past terminating separator or nul */
    return SH_PATH_NEXT(p);
}


/*
 *  Routine to verify that a file is present.  Note that this routine
 *  tolerates the presence of wildcards in the filespec.
 *
 *  The directory search order is the same as that in TOS2/TOS3/TOS4, as
 *  deduced from tests on those systems:
 *      (1) isolate the filename portion of pspec, and search for it in the
 *          application directory; if found, return the fully-qualified
 *          name, else continue.
 *      (2) search for pspec in the current directory; if found, return
 *          with pspec unchanged, else continue.
 *      (3) search for pspec in the root directory of the current drive;
 *          if found, return pspec with '\' prefixed, else continue.
 *      (4) search for pspec in each path of the AES path string; if found,
 *          return the fully-qualified name.
 *      (5) if still not found, return with error.
 */
static WORD findfile(char *pspec)
{
    char *path;
    char *pname;

    KDEBUG(("sh_find(): input pspec='%s'\n",pspec));
    pname = sh_name(pspec);                 /* get ptr to name      */

    dos_sdta(&SH_DTA);

    /* (1) search in the application directory */
    if (SH_APPDIR[0] != '\0')
    {
        if (SH_COPY(SH_WORK, SH_APPDIR, SH_WORK_BYTES) &&
            SH_APPEND(SH_WORK, pname, SH_WORK_BYTES) &&
            dos_sfirst(SH_WORK, FA_RO | FA_HIDDEN | FA_SYSTEM) == 0)   /* found */
        {
            strcpy(pspec, SH_WORK);
            KDEBUG(("sh_find(1): returning pspec='%s'\n",pspec));
            return 1;
        }
    }

    /* (2) search in the current directory */
    /* Fsfirst reads its filespec; no work-buffer copy is needed here. */
    if (dos_sfirst(pspec, FA_RO | FA_HIDDEN | FA_SYSTEM) == 0)   /* found */
    {
        KDEBUG(("sh_find(2): returning pspec='%s'\n",pspec));
        return 1;
    }

    /* (3) search in the root directory of the current drive */
    SH_WORK[0] = PATHSEP;
    if (SH_COPY(SH_WORK+1, pname, SH_WORK_BYTES-1) &&
        dos_sfirst(SH_WORK, FA_RO | FA_HIDDEN | FA_SYSTEM) == 0)   /* found */
    {
        strcpy(pspec, SH_WORK);
        KDEBUG(("sh_find(3): returning pspec='%s'\n",pspec));
        return 1;
    }

    /* (4) search in the AES path */
    sh_envrn(&path, PATH_ENV);      /* find PATH= in the command tail */
    if (!path)
    {
        KDEBUG(("sh_find(): no AES path, '%s' not found\n",pspec));
        return 0;
    }
    if (!*path)                     /* skip nul after PATH= */
        SH_EMPTY_PATH(path);

    while(1)
    {
        path = sh_path(path, SH_WORK, pname);
        if (!path)                  /* end of PATH= */
            break;
        if (dos_sfirst(SH_WORK, FA_RO | FA_HIDDEN | FA_SYSTEM) == 0)   /* found */
        {
            strcpy(pspec, SH_WORK);
            KDEBUG(("sh_find(4): returning pspec='%s'\n",pspec));
            return 1;
        }
    }

    KDEBUG(("sh_find(): '%s' not found\n",pspec));
    return 0;
}

WORD sh_find(char *pspec)
{
    DTA *save_dta;
    WORD ret;

    save_dta = dos_gdta();      /* save, findfile() modifies it */
    ret = findfile(pspec);      /* do the actual shel_find() */
    dos_sdta(save_dta);         /* restore */

    return ret;
}


