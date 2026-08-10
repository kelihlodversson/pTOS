/*      DESKFUN.C       08/30/84 - 05/30/85             Lee Lorenzen    */
/*                      10/2/86  - 01/16/87             MDF             */
/*      merge source    5/27/87  - 5/28/87              mdf             */
/*      for 2.3         6/11/87                         mdf             */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2001 John Elliott
*                 2002-2017 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Desktop                                       Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1985 - 1987               Digital Research Inc.
*       -------------------------------------------------------------
*/

/* #define ENABLE_KDEBUG */

#include <stdarg.h>
#include "config.h"
#include "portab.h"
#include "obdefs.h"
#include "dos.h"
#include "gemdos.h"
#include "optimize.h"

#include "deskbind.h"
#include "deskglob.h"
#include "deskapp.h"
#include "deskfpd.h"
#include "deskwin.h"
#include "gembind.h"
#include "aesbind.h"
#include "deskmain.h"
#include "desksupp.h"
#include "deskdir.h"
#include "deskfun.h"
#include "deskinf.h"
#include "deskins.h"
#include "deskpro.h"
#include "biosdefs.h"

#include "string.h"
#include "gemerror.h"
#include "kprint.h"


/*
 * the following global is initialised to FALSE in fun_drag().
 * it will be set to TRUE by file2desk() (called indirectly by
 * fun_drag()) when a file has been dropped onto a desktop icon
 * representing an executable program.
 *
 * the global is returned by fun_drag(), and if TRUE, the desktop
 * will subsequently exit to allow the program to run.
 */
static BOOL exit_desktop;

#if CONF_WITH_SEARCH
static WORD fnodes_found;
static WNODE *search_window;
#endif

/*
 *  Issue an alert
 */
WORD fun_alert(WORD defbut, WORD stnum)
{
    rsrc_gaddr_rom(R_STRING, stnum, (void **)&G.a_alert);
    return form_alert(defbut, G.a_alert);
}


/*
 *  Issue an alert after merging in a variable
 *
 *  The following way of handling multiple types for the variable to be
 *  merged is a bit of a kludge, but at least we make an attempt to
 *  avoid obvious problems ...
 *
 *  The merge value is read as a pointer-sized slot (32 bits on both
 *  m68k and ARM) and forwarded verbatim to sprintf().  This works in
 *  practice because every value a caller can supply is at most
 *  pointer-sized: a char promoted to int for "%c", a char * for "%s",
 *  and a long for "%ld", the last guaranteed to fit by the
 *  _Static_assert() in the function body below.  sprintf() re-reads the
 *  value from its own varargs list with the type its format specifier
 *  demands (see doprintf() in util/doprintf.c), so a slot that is only re-interpreted
 *  as a smaller or equal-sized type never reads past the value; "%c"
 *  takes an int, which on m68k is 16 bits and on ARM 32, both no wider
 *  than the slot we forwarded.  The same pattern has shipped in upstream
 *  EmuTOS since 2019 and runs on real m68k hardware, so treat it as
 *  intentional rather than something to "fix".
 *
 *  The varargs list is contracted to hold exactly one merge value: only
 *  the first argument is read and forwarded to sprintf().  Every alert
 *  string used with this function therefore has exactly one conversion
 *  specifier (STDISKFU/STDELDIS "%c", STRMVLOC "%s", STFMTINF "%ld"),
 *  and passing a string with more specifiers than values would make
 *  sprintf() read past the end of its argument list.  Do not use this
 *  function to merge several values without rewriting it first (e.g. by
 *  going through a vsprintf()-style helper that takes a va_list).
 */
WORD fun_alert_merge(WORD defbut, WORD stnum, ...)
{
    va_list ap;
    _Static_assert(sizeof(void *) >= sizeof(long), "incompatible type sizes");

    va_start(ap, stnum);
    rsrc_gaddr_rom(R_STRING, stnum, (void **)&G.a_alert);
    sprintf(G.g_1text, G.a_alert, va_arg(ap, void *));
    va_end(ap);

    return form_alert(defbut, G.g_1text);
}


void fun_msg(WORD type, WORD w3, WORD w4, WORD w5, WORD w6, WORD w7)
{
    /* keep DESKTOP messages internal to DESKTOP -- no AES call     */
    G.g_rmsg[0] = type;
    G.g_rmsg[1] = gl_apid;
    G.g_rmsg[2] = 0;
    G.g_rmsg[3] = w3;
    G.g_rmsg[4] = w4;
    G.g_rmsg[5] = w5;
    G.g_rmsg[6] = w6;
    G.g_rmsg[7] = w7;
    hndl_msg();
}


/*
 *  Mark window nodes for rebuild
 */
void fun_mark_for_rebld(BYTE *path)
{
    WNODE *pwin;

    for (pwin = G.g_wfirst; pwin; pwin = pwin->w_next)
    {
        /* if opened and same path then mark */
        if ( (pwin->w_id) && (strcmp(pwin->w_pnode.p_spec, path)==0) )
            pwin->w_flags |= WN_REBUILD;
    }
}


/*
 *  Rebuild a window
 */
static void rebuild_window(WNODE *pwin)
{
    GRECT gr;

    pn_active(&pwin->w_pnode, TRUE);
    desk_verify(pwin->w_id, TRUE);
    win_sinfo(pwin);
    wind_set(pwin->w_id, WF_INFO, pwin->w_info, 0, 0);
    wind_get_grect(pwin->w_id, WF_WXYWH, &gr);
    fun_msg(WM_REDRAW, pwin->w_id, gr.g_x, gr.g_y, gr.g_w, gr.g_h);
}


/*
 *  Rebuild marked windows
 */
void fun_rebld_marked(void)
{
    WNODE *pwin;

    graf_mouse(HGLASS, NULL);

    /* check all wnodes     */
    for (pwin = G.g_wfirst; pwin; pwin = pwin->w_next)
    {
        if (pwin->w_flags & WN_REBUILD)
        {
            rebuild_window(pwin);
            pwin->w_flags &= ~WN_REBUILD;
        }
    }

    graf_mouse(ARROW, NULL);
}


/*
 *  Rebuild any windows with matching path
 */
void fun_rebld(BYTE *ptst)
{
    WNODE *pwin;

    graf_mouse(HGLASS, NULL);

    /* check all wnodes     */
    for (pwin = G.g_wfirst; pwin; pwin = pwin->w_next)
    {
        /* if opened and same path then rebuild */
        if ( (pwin->w_id) && (strcmp(pwin->w_pnode.p_spec, ptst)==0) )
        {
            rebuild_window(pwin);
        } /* if */
    } /* for */

    graf_mouse(ARROW, NULL);
} /* fun_rebld */


#if CONF_WITH_SELECTALL
/*
 *  Select all files/folders in a window
 *
 *  Note: unlike Atari TOS, this can only select icons that currently have
 *  a screen object allocated (see fnode_is_selected()), so items scrolled
 *  out of view are not marked selected.
 */
void fun_selectall(WNODE *pw)
{
    GRECT gr;
    FNODE *pf;

    /* paranoia - check for desktop pseudo-window */
    if (pw->w_root == DROOT)
        return;

    for (pf = pw->w_pnode.p_flist; pf; pf = pf->f_next)
    {
        if (pf->f_obid != NIL)
            G.g_screen[pf->f_obid].ob_state |= SELECTED;
    }

    win_sinfo(pw);
    wind_get_grect(pw->w_id, WF_WXYWH, &gr);
    fun_msg(WM_REDRAW, pw->w_id, gr.g_x, gr.g_y, gr.g_w, gr.g_h);
}
#endif


#if CONF_WITH_FILEMASK
/*
 *  Routine to update the file mask for the current window
 */
void fun_mask(WNODE *pw)
{
    BYTE *maskptr, filemask[LEN_ZFNAME];
    OBJECT *tree;

    tree = G.a_trees[ADFMASK];

    /*
     * get current filemask & insert in dialog
     */
    maskptr = filename_start(pw->w_pnode.p_spec);
    fmt_str(maskptr, filemask);
    inf_sset(tree, FMMASK, filemask);

    /*
     * get user input
     */
    inf_show(tree, ROOT);

    /*
     * if 'OK', extract filemask from dialog, update pnode/display
     */
    if (inf_what(tree, FMOK, FMCANCEL) == 1)
    {
        inf_sget(tree, FMMASK, filemask);
        unfmt_str(filemask, maskptr);
        refresh_window(pw);
    }
}
#endif


/*
 *  Routine that creates a new directory in the specified window/path
 */
WORD fun_mkdir(WNODE *pw_node)
{
    PNODE *pp_node;
    OBJECT *tree;
    WORD  i, len, err;
    BYTE  fnew_name[LEN_ZFNAME], unew_name[LEN_ZFNAME], *ptmp;
    BYTE  path[MAXPATHLEN];

    tree = G.a_trees[ADMKDBOX];
    pp_node = &pw_node->w_pnode;
    ptmp = path;
    strcpy(ptmp, pp_node->p_spec);

    i = 0;
    while (*ptmp++)
    {
        if (*ptmp == '\\')
            i++;
    }

    if (i > MAX_LEVEL)
    {
        fun_alert(1, STFO8DEE);
        return FALSE;
    }

    while(1)
    {
        fnew_name[0] = '\0';
        inf_sset(tree, MKNAME, fnew_name);
        start_dialog(tree);
        form_do(tree, 0);
        if (inf_what(tree, MKOK, MKCNCL) == 0)
            break;

        inf_sget(tree, MKNAME, fnew_name);
        unfmt_str(fnew_name, unew_name);

        if (unew_name[0] == '\0')
            break;

        ptmp = add_fname(path, unew_name);
        err = dos_mkdir(path);
        if (err == 0)       /* mkdir succeeded */
        {
            fun_rebld(pw_node->w_pnode.p_spec);
            break;
        }

        /*
         * if we're getting a BIOS (rather than GEMDOS) error, the
         * critical error handler has already issued a message, so
         * just quit
         */
        if (IS_BIOS_ERROR(err))
            break;

        len = strlen(path); /* before we restore old path */
        restore_path(ptmp); /* restore original path */
        if (len >= LEN_ZPATH-3)
        {
            fun_alert(1,STDEEPPA);
            break;
        }

        /*
         * mkdir failed with a recoverable error:
         * prompt for Cancel or Retry
         */
        if (fun_alert(2,STFOFAIL) == 1)     /* Cancel */
            break;
    }

    end_dialog(tree);
    return TRUE;
}


/*
 *  return pointer to next folder in path.
 *  start at the current position of the ptr.
 *  assume path will eventually end with \*.*
 */
static BYTE *ret_path(BYTE *pcurr)
{
    BYTE *path;

    /* find next level */
    while( (*pcurr) && (*pcurr != '\\') )
        pcurr++;
    pcurr++;

    /* get to current position */
    path = pcurr;

    /* find end of curr level */
    while( (*path) && (*path != '\\') )
        path++;

    *path = '\0';
    return(pcurr);
} /* ret_path */


/*
 *  Check to see if source is a parent of the destination.
 *  If it is, issue alert & return TRUE; otherwise just return FALSE.
 *  Must assume that src and dst paths both end with "\*.*".
 */
static WORD source_is_parent(BYTE *psrc_path, FNODE *pflist, BYTE *pdst_path)
{
    BYTE *tsrc, *tdst;
    WORD same;
    FNODE *pf;
    BYTE srcpth[MAXPATHLEN];
    BYTE dstpth[MAXPATHLEN];

    if (psrc_path[0] != pdst_path[0])   /* check drives */
        return FALSE;

    tsrc = srcpth;
    tdst = dstpth;
    same = TRUE;
    do
    {
        /* new copies */
        strcpy(srcpth, psrc_path);
        strcpy(dstpth, pdst_path);

        /* get next paths */
        tsrc = ret_path(tsrc);
        tdst = ret_path(tdst);
        if ( strcmp(tsrc, "*.*") )
        {
            if ( strcmp(tdst, "*.*") )
                same = strcmp(tdst, tsrc);
            else
                same = FALSE;
        }
        else
        {
            /* check to same level */
            if ( !strcmp(tdst, "*.*") )
                same = FALSE;
            else
            {
                /* walk file list */
                for (pf = pflist; pf; pf = pf->f_next)
                {
                    /* exit if same subdir  */
                    if ( fnode_is_selected(pf) &&
                        (pf->f_attr & F_SUBDIR) &&
                        (!strcmp(pf->f_name, tdst)) )
                    {
                        /* INVALID      */
                        fun_alert(1, STBADCOP);
                        return TRUE;
                    }
                }
                same = FALSE;   /* ALL OK */
            }
        }
    } while(same);

    return FALSE;
}


/*
 *  Perform the operation 'op' on all the files & folders in the
 *  path associated with 'pspath'.  'op' can be OP_DELETE, OP_COPY,
 *  OP_MOVE.
 */
WORD fun_op(WORD op, WORD icontype, PNODE *pspath, BYTE *pdest)
{
    DIRCOUNT count;

    switch(op)
    {
    case OP_COPY:
    case OP_MOVE:
        if (source_is_parent(pspath->p_spec, pspath->p_flist, pdest))
            return FALSE;
        /* drop thru */
    case OP_DELETE:
        dir_op(OP_COUNT, icontype, pspath, pdest, &count);  /* get count of source files */
        if ((count.files+count.dirs) == 0)
            break;
        dir_op(op, icontype, pspath, pdest, &count);        /* do the operation     */
        return TRUE;
    }

    return FALSE;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   D E S K 1   r o u t i n e s                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

static void w_setpath(WNODE *pw, BYTE *pathname)
{
    WORD icx, icy;
    GRECT rc;

    wind_get_grect(pw->w_id,WF_WXYWH, &rc);

    icx = rc.g_x + (rc.g_w / 2) - (G.g_wicon / 2);
    icy = rc.g_y + (rc.g_h / 2) - (G.g_hicon / 2);
    graf_shrinkbox(icx, icy, G.g_wicon, G.g_hicon,
                    rc.g_x, rc.g_y, rc.g_w, rc.g_h);

    /* we're closing a folder, so we never want a new window */
    pw->w_cvrow = 0;        /* must reset slider */
    do_fopen(pw, 0, pathname, FALSE);
}


static void fun_full_close(WNODE *pw)
{
    WORD icx, icy;
    GRECT rc;

    wind_get_grect(pw->w_id,WF_WXYWH, &rc);
    wind_close(pw->w_id);

    /*
     * only do the shrinkbox effect if there is a matching icon
     */
    if (pw->w_obid > 0)
    {
        icx = G.g_screen[pw->w_obid].ob_x;
        icy = G.g_screen[pw->w_obid].ob_y;
        graf_shrinkbox(icx, icy, G.g_wicon, G.g_hicon,
                        rc.g_x, rc.g_y, rc.g_w, rc.g_h);
    }

    pn_close(&pw->w_pnode);
    win_free(pw);

    /*
     * update current window etc
     */
    pw = win_ontop();
    desk_verify(pw ? pw->w_id : 0, FALSE);
}


/*
 *  Removes the lowest level of folder from a pathname, assumed
 *  to be of the form:
 *      D:\X\Y\Z\F.E
 *  where X,Y,Z are folders and F.E is a filename.  In the above
 *  example, this would change D:\X\Y\Z\F.E to D:\X\Y\F.E
 */
static void remove_one_level(BYTE *pathname)
{
    BYTE *stop = pathname+2;    /* the first path separator */
    BYTE *filename, *prev;

    filename = filename_start(pathname);
    if (filename-1 <= stop)     /* already at the root */
        return;

    for (prev = filename-2; prev >= stop; prev--)
        if (*prev == '\\')
            break;

    strcpy(prev+1,filename);
}


#if CONF_WITH_SEARCH
/*
 *  Converts string to wildcard-format spec
 *
 *  the string is obtained from the dialog box via unfmt_str()
 *
 *  sample conversions:
 *      "A"         => "A*.*"
 *      ".DOC"      => "*.DOC"
 *      "X?Y.?Z"    => "X?Y*.?Z*"
 */
static void convert(BYTE *wildcard, BYTE *str)
{
    WORD i;
    BYTE *p, *q;

    /* convert name */
    for (i = 0, p = str, q = wildcard; i < 8; i++)
    {
        if (!*p || (*p == '.'))
        {
            *q++ = '*';
            break;
        }
        if (*p == '*')
        {
            *q++ = *p++;
            break;
        }
        *q++ = *p++;
    }
    *q++ = '.';

    /* look for end of name */
    while (TRUE)
    {
        if (!*p)
            break;
        if (*p++ == '.')
            break;
    }

    /* convert type */
    for (i = 0; i < 3; i++)
    {
        if (!*p)
        {
            *q++ = '*';
            break;
        }
        if (*p == '*')
        {
            *q++ = *p++;
            break;
        }
        *q++ = *p++;
    }
    *q++ = '\0';
}


/*
 *  Prompt for Search specification
 *
 *  returns FALSE if Cancel, or specification is empty
 */
static BOOL search_prompt(BYTE *searchname)
{
    BYTE filemask[LEN_ZFNAME];
    OBJECT *tree;

    tree = G.a_trees[ADSEARCH];

    /*
     * clear any leftover value in dialog
     */
    inf_sset(tree, SFNAME, "");

    /*
     * get user input & if not 'OK', return FALSE
     */
    inf_show(tree, ROOT);
    if (inf_what(tree, SFOK, SFCANCEL) != 1)
        return FALSE;

    /*
     * extract searchname from dialog
     *
     * returns TRUE iff the input is not empty
     */
    inf_sget(tree, SFNAME, filemask);
    unfmt_str(filemask, searchname);

    return *searchname ? TRUE : FALSE;
}


/*
 *  Mark files/folders matching specification and, if one or more
 *  are found, redisplays window with the first-found match as high
 *  as possible within the window
 *
 *  if no matches are found, returns FALSE and does not redisplay
 */
static BOOL mark_matching_fnodes(WNODE *pw, BYTE *searchwild)
{
    WORD first_match = -1, n;
    FNODE *pf;
    GRECT gr;

    /*
     * first pass: just find the first match, so we know where to scroll to.
     * (also counts every match found, visible or not, for fun_search()'s
     * "no more files" alert)
     */
    for (pf = pw->w_pnode.p_flist, n = 0; pf; pf = pf->f_next, n++)
    {
        if (wildcmp(searchwild, pf->f_name))
        {
            fnodes_found++;
            if (first_match < 0)
                first_match = n;
        }
    }
    if (first_match < 0)
        return FALSE;

    /*
     * scroll to the first match *before* marking selections: this may
     * allocate screen objects for FNODEs that had none (see
     * fnode_is_selected()), including the first match itself if it was
     * off-screen
     */
    win_dispfile(pw, first_match);

    /*
     * second pass: select all matching FNODEs that now have a screen object
     */
    for (pf = pw->w_pnode.p_flist; pf; pf = pf->f_next)
    {
        if ((pf->f_obid != NIL) && wildcmp(searchwild, pf->f_name))
            G.g_screen[pf->f_obid].ob_state |= SELECTED;
    }

    /*
     * update info line & force a redraw: win_dispfile() above only redraws
     * if it had to scroll, but the selection highlight needs to be drawn
     * even when the first match was already visible
     */
    win_sinfo(pw);
    wind_get_grect(pw->w_id, WF_WXYWH, &gr);
    fun_msg(WM_REDRAW, pw->w_id, gr.g_x, gr.g_y, gr.g_w, gr.g_h);

    return TRUE;
}


/*
 *  Display a folder with matching FNODEs marked
 */
static BOOL search_display(WORD curr, BYTE *pathname, BYTE *searchwild)
{
    BOOL newwin = FALSE;

    if (!search_window)
    {
        search_window = win_alloc(curr);
        if (!search_window)
        {
            fun_alert(1, STNOWIND);
            return FALSE;
        }
        newwin = TRUE;
    }

    /*
     * we open the new path, after closing the previous one (which
     * doesn't exist if this is a new window)
     */
    if (!newwin)
        pn_close(&search_window->w_pnode);
    if (!do_diropen(search_window, newwin, curr, pathname,
                    (GRECT *)&G.g_screen[search_window->w_root].ob_x, FALSE))
        return FALSE;   /* bad pathname or error reading directory */

    /*
     * now mark matching FNODEs
     */
    mark_matching_fnodes(search_window, searchwild);

    /*
     *  we marked one or more FNODEs, ask if user wants to continue
     */
    if (fun_alert(1, STCNSRCH) != 1)
        return FALSE;   /* user cancelled */

    return TRUE;
}


/*
 *  Recursively search folder icons
 *
 *  returns FALSE iff we should stop immediately, e.g. because user cancelled
 */
static BOOL search_recursive(WORD curr, BYTE *pathname, BYTE *searchwild)
{
    DTA dta, *save_dta;
    BYTE *p;
    WORD ret;
    BOOL ok;

    /*
     * we must use a local DTA to manage the recursive search
     */
    save_dta = dos_gdta();
    dos_sdta(&dta);

    /*
     * check if there is a filename match; if so, display the folder
     *
     * if 'searchwild' (unlike the "*.*" it temporarily replaces) doesn't
     * fit in the space remaining in 'pathname', treat this folder as a
     * non-match rather than overflow the caller's buffer
     */
    p = filename_start(pathname);
    if (strlen(searchwild) < (WORD)(MAXPATHLEN - (p - pathname)))
    {
        strcpy(p, searchwild);
        ret = dos_sfirst(pathname, F_SUBDIR);
        strcpy(p, "*.*");
    }
    else
    {
        ret = ENMFIL;
    }
    dos_sdta(save_dta); /* in case we must return */

    switch(ret) {
    case 0:             /* file found, display folder */
        if (!search_display(curr, pathname, searchwild))
            return FALSE;   /* user cancelled */
        FALLTHROUGH;
    case ENMFIL:        /* nothing found, continue processing */
    case EFILNF:
        break;
    default:            /* some strange kind of error, ignore silently */
        return TRUE;
    }

    /*
     * at this point, either there were no matching filenames, or we found
     * some but the user wants to continue.  we do an fsfirst/fsnext loop
     * and call ourselves for every folder found.
     */
    dos_sdta(&dta);     /* original DTA is already saved */

    for (ret = dos_sfirst(pathname, F_SUBDIR), ok = TRUE; ret == 0; ret = dos_snext())
    {
        if (dta.d_fname[0] == '.')  /* ignore . and .. */
            continue;

        if (dta.d_attrib & F_SUBDIR)
        {
            if (!add_one_level(pathname, dta.d_fname))
                continue;   /* pathname is too long, silently ignore: FIXME */
            ok = search_recursive(0, pathname, searchwild);
            remove_one_level(pathname);
            if (!ok)
                break;
        }
    }

    dos_sdta(save_dta);

    /*
     * by design, errors from fsfirst/fsnext are ignored
     */
    return ok;
}


/*
 *  Process the specified icon
 *
 *  returns TRUE iff we should continue
 */
static BOOL search_icon(WORD win, WORD curr, BYTE *searchwild)
{
    ANODE *pa;
    FNODE *pf;
    BYTE pathname[MAXPATHLEN];
    BYTE *p;

    pa = i_find(win, curr, &pf, NULL);
    if (!pa)
        return TRUE;

    switch(pa->a_type) {
    case AT_ISFOLD:
#if CONF_WITH_DESKTOP_SHORTCUTS
        if (pa->a_flags & AF_ISDESK)
        {
            strcpy(pathname, pa->a_pdata);
        }
        else
#endif
        {
            WNODE *temp = win_find(win);
            strcpy(pathname, temp->w_pnode.p_spec);
            strcpy(filename_start(pathname), pf->f_name);
        }
        break;
    case AT_ISDISK:
        p = pathname;
        *p++ = pa->a_letter;
        *p++ = ':';
        *p = '\0';
        break;
    default:            /* do nothing for file, trash or printer icon */
        return TRUE;
    }

    strcat(pathname, "\\*.*");

    if (!search_recursive(curr, pathname, searchwild))
        return FALSE;   /* propagate error to fun_search() */

    return TRUE;
}


/*
 *  Perform the desktop Search function
 */
void fun_search(WORD curr, WNODE *pw)
{
    BYTE searchname[LEN_ZFNAME], searchwild[LEN_ZFNAME];

    if (!search_prompt(searchname))     /* prompt for name to search for */
        return;

    convert(searchwild, searchname);    /* convert to standard wildcard */

    /*
     * if there are one or more highlighted icons, process them
     */
    fnodes_found = 0;
    if (curr)
    {
        WORD win = G.g_cwin;    /* save because the global variables */
        WORD root = G.g_croot;  /*  will be changed by search_icon() */
        GRECT gr;

        search_window = NULL;
        for ( ; curr; curr = win_isel(G.g_screen, root, curr))
        {
            if (!search_icon(win, curr, searchwild))
                return;         /* user cancelled search */
        }
        if (fnodes_found)
        {
            fun_alert(1, STNOMORE); /* no more files */
            wind_get_grect(win, WF_WXYWH, &gr);
            do_wredraw(win, gr.g_x, gr.g_y, gr.g_w, gr.g_h);   /* redraw the original window (may be desktop) */
            return;
        }
    }
    else    /* otherwise handle an open window with no highlighted icons */
    {
        mark_matching_fnodes(pw, searchwild);
    }

    if (!fnodes_found)
        fun_alert_merge(1, STFILENF, searchname);
}
#endif


/*
 * full or partial close of desktop window
 */
void fun_close(WNODE *pw, WORD closetype)
{
    BYTE pathname[MAXPATHLEN];
    BYTE *fname;

    graf_mouse(HGLASS, NULL);

    /*
     * handle CLOSE_FOLDER and CLOSE_TO_ROOT
     *
     * if already in the root, change CLOSE_FOLDER to CLOSE_WINDOW
     * (but don't change CLOSE_TO_ROOT!)
     */
    if (closetype != CLOSE_WINDOW)
    {
        strcpy(pathname,pw->w_pnode.p_spec);
        fname = filename_start(pathname);
        if (closetype == CLOSE_TO_ROOT)
            strcpy(pathname+3,fname);
        else if (pathname+3 == fname)
            closetype = CLOSE_WINDOW;
        else    /* we need to go up one level */
            remove_one_level(pathname);
    }

    if (closetype == CLOSE_WINDOW)
        fun_full_close(pw);
    else
        w_setpath(pw,pathname);

    graf_mouse(ARROW, NULL);
}


/*
 * builds the path corresponding to the first selected file in the
 * specified PNODE.  the path will be the filename only, or the full
 * pathname, depending on the desktop configuration settings.
 *
 * returns FALSE if no file is selected (probable program bug)
 */
static BOOL build_selected_path(PNODE *pn, BYTE *pathname)
{
    FNODE *fn;

    for (fn = pn->p_flist; fn; fn = fn->f_next)
    {
        if (fnode_is_selected(fn))
            break;
    }
    if (!fn)
        return FALSE;

#if CONF_WITH_DESKTOP_CONFIG
    if (G.g_fullpath)
    {
        strcpy(pathname,pn->p_spec);
        add_fname(pathname,fn->f_name);
    }
    else
#endif
    {
        strcpy(pathname,fn->f_name);
    }

    return TRUE;
}


/*
 *  Routine to call when several icons have been dragged from a
 *  window to another window (it might be the same window) and
 *  dropped on a particular icon or open space.
 *
 *  This can be invoked when copying/moving files, or when launching
 *  a program via drag-and-drop.
 *
 *  Note that this is NEVER called if either the source or destination
 *  is the desktop.  Thus 'datype' can ONLY be AT_ISFILE or AT_ISFOLD.
 */
static void fun_win2win(WORD src_wh, WORD dst_wh, WORD dst_ob, WORD keystate)
{
    WORD  ret, datype, op;
    WNODE *psw, *pdw;
    ANODE *pda;
    FNODE *pdf;
    BYTE  destpath[MAXPATHLEN];

    op = (keystate&MODE_CTRL) ? OP_MOVE : OP_COPY;
    psw = win_find(src_wh);
    if (!psw)
        return;
    pdw = win_find(dst_wh);
    if (!pdw)
        return;

    pda = i_find(dst_wh, dst_ob, &pdf, NULL);

    if (pda)
    {
        if (pda->a_aicon >= 0)      /* dropping file on to an application */
        {
            if (build_selected_path(&psw->w_pnode, destpath))
            {
                /* set global so desktop will exit if do_aopen() succeeds */
                exit_desktop = do_aopen(pda, 1, dst_ob, pdw->w_pnode.p_spec, pdf->f_name, destpath);
                return;
            }
        }
        datype = pda->a_type;
    }
    else
    {
        datype = AT_ISFILE;
    }

    /* set up default destination path name */
    strcpy(destpath, pdw->w_pnode.p_spec);

    /* if destination is folder, insert folder name in path */
    if (datype == AT_ISFOLD)
        add_path(destpath, pdf->f_name);

    ret = fun_op(op, -1, &psw->w_pnode, destpath);

    if (ret)
    {
        if (src_wh != dst_wh)
            desk_clear(src_wh);
        if (op == OP_MOVE)
            fun_rebld(psw->w_pnode.p_spec);
        fun_rebld(pdw->w_pnode.p_spec);
        /*
         * if we copied into a folder, we must redraw any windows with
         * a matching path
         */
        if (datype == AT_ISFOLD)
            fun_rebld(destpath);
    }
}


static WORD fun_file2desk(PNODE *pn_src, WORD icontype_src, ANODE *an_dest, WORD dobj, WORD keystate)
{
    ICONBLK *dicon;
    BYTE pathname[MAXPATHLEN];
    WORD operation, ret;

    pathname[1] = ':';      /* set up everything except drive letter */
    strcpy(pathname+2, "\\*.*");

    operation = -1;
    if (an_dest)
    {
        switch(an_dest->a_type)
        {
#if CONF_WITH_DESKTOP_SHORTCUTS
        BYTE tail[MAXPATHLEN];

        case AT_ISFILE:     /* dropping something onto a file */
            if (an_dest->a_aicon < 0)       /* is target a program? */
                break;                      /* no, do nothing */

            /* build the full tail to pass to the target program */
            if (!build_selected_path(pn_src,tail))
                break;

            /* build pathname for do_aopen() */
            strcpy(pathname,an_dest->a_pdata);
            strcpy(filename_start(pathname),"*.*");

            /* set global so desktop will exit if do_aopen() succeeds */
            exit_desktop = do_aopen(an_dest, 1, dobj, pathname, an_dest->a_pappl, tail);
            break;
        case AT_ISFOLD:     /* dropping file on folder - copy or move */
            strcpy(pathname,an_dest->a_pdata);
            strcat(pathname,"\\*.*");
            operation = (keystate&MODE_CTRL) ? OP_MOVE : OP_COPY;
            break;
#endif
        case AT_ISDISK:
            dicon = G.g_screen[dobj].ob_spec.iconblk;
            pathname[0] = LOBYTE(dicon->ib_char);
            operation = (keystate&MODE_CTRL) ? OP_MOVE : OP_COPY;
            break;
        case AT_ISTRSH:
            if (icontype_src >= 0)      /* source is desktop icon */
                if (wants_to_delete_files() == FALSE)
                    return FALSE;       /* i.e. remove icons or cancel */
            pathname[0] = pn_src->p_spec[0];
            operation = OP_DELETE;
            break;
        }
    }

    if (operation >= 0)
        ret = fun_op(operation, icontype_src, pn_src, pathname);
    else ret = FALSE;

    /*
     * if operation succeeded, rebuild any corresponding open windows
     */
    if (ret)
        fun_rebld(pathname);

    return ret;
}


static WORD fun_file2win(PNODE *pn_src, BYTE  *spec, ANODE *an_dest, FNODE *fn_dest)
{
    BYTE *p;
    BYTE pathname[MAXPATHLEN];

    strcpy(pathname, spec);

    p = filename_start(pathname);

    if (an_dest && an_dest->a_type == AT_ISFOLD)
    {
        strcpy(p, fn_dest->f_name);
        strcat(p, "\\*.*");
    }
    else
    {
        strcpy(p, "*.*");
    }

    return fun_op(OP_COPY, -1, pn_src, pathname);
}


static void fun_win2desk(WORD wh, WORD obj, WORD keystate)
{
    WNODE *wn_src;
    ANODE *an_dest;

    an_dest = app_afind_by_id(obj);
    if (!an_dest)   /* "can't happen" */
        return;

    wn_src = win_find(wh);
    if (!wn_src)
        return;

    if (fun_file2desk(&wn_src->w_pnode, -1, an_dest, obj, keystate))
        fun_rebld(wn_src->w_pnode.p_spec);
}


static WORD fun_file2any(WORD sobj, WNODE *wn_dest, ANODE *an_dest, FNODE *fn_dest,
                  WORD dobj, WORD keystate)
{
    WORD icontype, okay = 0;
    FNODE *bp8;
    ICONBLK * ib_src;
    PNODE *pn_src;
    ANODE *an_src;
    BYTE path[MAXPATHLEN];

    an_src = i_find(0, sobj, NULL, NULL);

#if CONF_WITH_DESKTOP_SHORTCUTS
    if ((an_src->a_type == AT_ISFILE) || (an_src->a_type == AT_ISFOLD))
    {
        strcpy(path, an_src->a_pdata);
    }
    else
#endif
    {
        ib_src = G.g_screen[sobj].ob_spec.iconblk;
        build_root_path(path, ib_src->ib_char);
        strcat(path,"*.*");
    }

    pn_src = pn_open(path, NULL);

    if (pn_src)
    {
        okay = pn_active(pn_src, FALSE);

        if (pn_src->p_flist)
        {
            for (bp8 = pn_src->p_flist; bp8; bp8 = bp8->f_next)
                bp8->f_obid = 0;
            /*
             * if we do not set the root's SELECTED attribute, dir_op()
             * (which is called by fun_file2win() & fun_file2desk())
             * will not process any of these files ...
             */
            G.g_screen->ob_state = SELECTED;
            if (wn_dest)    /* we are dragging a desktop icon to a window */
            {
                okay = fun_file2win(pn_src, wn_dest->w_pnode.p_spec, an_dest, fn_dest);
            }
            else    /* we are dragging a desktop item to another desktop item */
            {
                icontype = an_src ? an_src->a_type : -1;
                okay = fun_file2desk(pn_src, icontype, an_dest, dobj, keystate);
            }
            G.g_screen->ob_state = 0;
        }
        pn_close(pn_src);
        desk_clear(0);
    }

    return okay;
}


static void fun_desk2win(WORD wh, WORD dobj, WORD keystate)
{
    WNODE *wn_dest;
    FNODE *fn_dest;
    WORD sobj, copied;
    ANODE *an_src, *an_dest;

    wn_dest = win_find(wh);
    if (!wn_dest)
        return;

    an_dest = i_find(wh, dobj, &fn_dest, NULL);
    sobj = 0;
    while ((sobj = win_isel(G.g_screen, DROOT, sobj)))
    {
        an_src = i_find(0, sobj, NULL, NULL);
        if (an_src && (an_src->a_type == AT_ISTRSH))
        {
            fun_alert(1, STNODRA2);
            continue;
        }
        copied = fun_file2any(sobj, wn_dest, an_dest, fn_dest, dobj, keystate);
        if (copied)
            fun_rebld(wn_dest->w_pnode.p_spec);
    }
}


static void fun_desk2desk(WORD dobj, WORD keystate)
{
    WORD sobj;
    ANODE *source;
    ANODE *target;

    target = app_afind_by_id(dobj);
    if (!target)    /* "can't happen" */
        return;

    sobj  = 0;
    while ((sobj = win_isel(G.g_screen, DROOT, sobj)))
    {
        source = i_find(0, sobj, NULL, NULL);
        if (!source || (source == target))
            continue;
        if (source->a_type == AT_ISTRSH)
        {
            fun_alert(1, STNOSTAK);
            continue;
        }
        fun_file2any(sobj, NULL, target, NULL, dobj, keystate);
    }
}


BOOL fun_drag(WORD wh, WORD dest_wh, WORD sobj, WORD dobj, WORD mx, WORD my, WORD keystate)
{
    exit_desktop = FALSE;   /* may be set to TRUE by fun_file2desk() */

    if (wh)
    {
        if (dest_wh)    /* dragging from window to window, */
        {               /* e.g. copy/move files/folders    */
            fun_win2win(wh, dest_wh, dobj, keystate);
        }
        else            /* dragging from window to desktop */
        {
            if (dobj == DROOT)  /* dropping onto desktop surface */
            {
#if CONF_WITH_DESKTOP_SHORTCUTS
                ins_shortcut(wh, mx, my);
#else
                fun_alert(1, STNODRA1);
#endif
            }
            else                /* dropping onto desktop icon */
                fun_win2desk(wh, dobj, keystate);
        }
    }
    else    /* Dragging something from desk */
    {
        if (dest_wh)    /* dragging from desktop to window,  */
        {               /* e.g. copying a disk into a folder */
            fun_desk2win(dest_wh, dobj, keystate);
        }
        else            /* dragging from desktop to desktop,   */
        {               /* e.g. copying a disk to another disk */
            fun_desk2desk(dobj, keystate);
        }
    }

    return exit_desktop;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   e n d   o f   D E S K 1   r o u t i n e s                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


/*
 * Function called to delete the contents of a disk
 */
static WORD delete_disk(ANODE *pa)
{
    PNODE *pn;
    FNODE *fn;
    BYTE path[10];
    WORD ret = 0;

    build_root_path(path, pa->a_letter);
    strcat(path,"*.*");
    pn = pn_open(path, NULL);
    if (pn == NULL)     /* "can't happen" - pathname too long! */
        return 0;

    graf_mouse(HGLASS, NULL);
    pn_active(pn, TRUE);
    if (pn->p_flist)
    {
        /*
         * point all the FNODEs to the root, then set the root's
         * SELECTED attribute; this is a cheap way of making dir_op()
         * (called by fun_op()) think all the files are selected
         */
        for (fn = pn->p_flist; fn; fn = fn->f_next)
            fn->f_obid = 0;
        G.g_screen->ob_state = SELECTED;
        ret = fun_op(OP_DELETE, pa->a_type, pn, NULL);
        G.g_screen->ob_state = 0;   /* reset for safety */
    }
    pn_close(pn);
    graf_mouse(ARROW, NULL);

    return ret;
}

/*
 *  This routine is called when the 'Delete' menu item is selected
 */
void fun_del(WORD sobj)
{
    ANODE *pa;
    WNODE *pw;
    WORD disk_found = 0;

    /*
     * if the item selected is on the desktop, there may be other desktop
     * items that have been selected; make sure we process all of them
     */
    if ( (pa = i_find(0, sobj, NULL, NULL)) )
    {
        if (wants_to_delete_files() == FALSE)   /* i.e. remove icons or cancel */
            return;
        for ( ; sobj; sobj = win_isel(G.g_screen, DROOT, sobj))
        {
            pa = i_find(0,sobj,NULL,NULL);
            if (!pa)
                continue;
            if (pa->a_type == AT_ISDISK)
            {
                disk_found++;
                if (delete_disk(pa))
                    refresh_drive(pa->a_letter);
            }
        }
        if (disk_found)
        {
            desk_clear(0);
            return;
        }
    }

    /*
     * otherwise, process path associated with selected window icon, if any
     */
    pw = win_find(G.g_cwin);

    if (pw)
    {
        if (fun_op(OP_DELETE, -1, &pw->w_pnode, NULL))
            fun_rebld(pw->w_pnode.p_spec);
    }
}

/*
 * prompt for delete files or remove icons
 *
 * if user selects Delete, returns TRUE
 * if user selects Remove, sends a message to remove icons & returns FALSE
 * else returns FALSE
 */
BOOL wants_to_delete_files(void)
{
    WORD ret;

    ret = fun_alert(1,STRMVDEL);

    if (ret == 2)       /* Delete */
        return TRUE;

    if (ret == 1)       /* Remove */
        fun_msg(MN_SELECTED,OPTNMENU,RICNITEM,0,0,0);

    return FALSE;       /* Remove or Cancel */
}
