/* Original GEM alert parsing and layout.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesext.h"
#include "intmath.h"
#include "gemobjop.h"
#include "gemoblib.h"
#include "gemobed.h"
#include "gemrslib.h"
#include "gemfmalt.h"
#include "gem_rsc.h"
#include "rectfunc.h"
#ifdef AES_ALERT_BINDINGS
#include AES_ALERT_BINDINGS
#else
#define ALERT_3D CONF_WITH_3D_OBJECTS
#endif

/* TOS standard form_alert() maximum values */
#define TOS_MAX_LINELEN 32
#define TOS_MAX_BUTLEN  10


#define NUM_ALOBJS   10     /* MUST match # of objects in the DIALERT tree! */
#define INTER_WSPACE 0
#define INTER_HSPACE 0

#if ALERT_3D
#define BUTTON_FLAGS    (SELECTABLE | EXIT | FL3DACT)
#else
#define BUTTON_FLAGS    (SELECTABLE | EXIT)
#endif


/*
 *  Routine to break a string into smaller strings.  Breaks occur
 *  whenever an | or a ] is encountered.
 *  Exception: An || or ]] does not break the string, but gives
 *  a literal | or ]. This matches the (undocumented) behavior
 *  of Atari TOS and PC GEM.
 *
 *  Input:  start       starting object
 *          maxnum      maximum number of substrings
 *          maxlen      maximum length of substring
 *          alert       starting point in alert string
 *  Output: pnum        number of substrings found
 *          plen        maximum length of substring found
 *  Returns:            pointer to next character to process
 */
#define endstring(a)    ( ((a)==']') || ((a)=='\0') )
#define endsubstring(a) ( ((a)=='|') || ((a)==']') || ((a)=='\0') )
#define isduplicate(a,b) ( (a!='\0') && ((a)==(b)) )

static char *fm_strbrk(OBJECT *start,WORD maxnum,WORD maxlen,char *alert,
                           WORD *pnum,WORD *plen)
{
    int i, j, len;
    OBJECT *obj;
    char *p;

    *plen = 0;

    if (*alert == '[')              /* ignore a leading [ */
        alert++;

    for (i = 0, obj = start; i < maxnum; i++, obj++, alert++) {
        p = obj->ob_spec.free_string;
        for (j = 0; j < maxlen; j++) {
            if (endsubstring(*alert)) {
                if (isduplicate(*alert,*(alert+1))) {
                    alert++;        /* || or [[ found: skip a character */
                } else {
                    break;
                }
            }
            *p++ = *alert++;
        }
        *p = '\0';

        len = p - obj->ob_spec.free_string;
        if (len > *plen)            /* track max substring length */
            *plen = len;

        if (!endsubstring(*alert)) {/* substring was too long */
            KDEBUG(("form_alert(): substring > %d bytes long\n",maxlen));
            while(1) {              /* eat rest of substring */
                if (endsubstring(*alert))
                    break;
                alert++;
            }
        }
        if (endstring(*alert))      /* end of all substrings */
            break;
    }
    if (i >= maxnum)                /* too many substrings */
        KDEBUG(("form_alert(): more than %d substrings\n",maxnum));

    while(1) {                      /* eat any remaining characters */
        if (endstring(*alert))
            break;
        alert++;
    }

    *pnum = (i<maxnum)?(i+1):maxnum;/* count of substrings found */

    if (*alert)                     /* if not at null byte, */
        alert++;                    /* point to next one    */

    return alert;
}


/*
 *  Routine to parse a string into an icon #, multiple message
 *  strings, and multiple button strings.  For example,
 *
 *      [0][This is some text|for the screen.][Ok|Cancel]
 *
 *  becomes:
 *      icon# = 0;
 *      1st msg line = This is some text
 *      2nd msg line = for the screen.
 *      1st button = Ok
 *      2nd button = Cancel
 *
 *  Input:  tree        address of tree
 *          palstr      pointer to alert string
 *  Output: pnummsg     number of message lines
 *          plenmsg     length of biggest line
 *          pnumbut     number of buttons
 *          plenbut     length of biggest button
 */
static void fm_parse(OBJECT *tree, char *palstr, WORD *picnum, WORD *pnummsg,
                         WORD *plenmsg, WORD *pnumbut, WORD *plenbut)
{
    OBJECT *obj = tree;
    char *alert = palstr;

    *picnum = alert[1] - '0';

    alert = fm_strbrk(obj+MSGOFF,MAX_LINENUM,MAX_LINELEN,alert+3,pnummsg,plenmsg);
    if (*plenmsg > TOS_MAX_LINELEN)
        KDEBUG(("form_alert(): warning: alert line(s) exceed TOS standard length\n"));

    fm_strbrk(obj+BUTOFF,MAX_BUTNUM,MAX_BUTLEN,alert,pnumbut,plenbut);
    if (*plenbut > TOS_MAX_BUTLEN)
        KDEBUG(("form_alert(): warning: alert button(s) exceed TOS standard length\n"));

    *plenbut += 1;  /* allow 1/2 character space inside each end of button */
}


/*
 *  Routine to build the alert
 *
 *  Inputs are:
 *      tree            the alert dialog
 *      iconnum         icon number, 0 => no icon
 *      nummsg          number of message lines
 *      mlenmsg         length of longest line
 *      numbut          number of buttons
 *      mlenbut         length of biggest button
 */
static void fm_build(OBJECT *tree, WORD iconnum, WORD nummsg, WORD mlenmsg,
                     WORD numbut, WORD mlenbut)
{
    WORD i, hicon, allbut;
    GRECT al, ic, bt, ms;
    OBJECT *obj;

    /*
     * we use the GRECTs as workareas for building the object character coordinates:
     *  'al'    the entire alert
     *  'ms'    the first message line
     *  'bt'    the first button
     *  'ic'    the icon
     */
    r_set(&al, 0, 0, 1+INTER_WSPACE, 1+INTER_HSPACE);
    r_set(&ms, 1+INTER_WSPACE, 1+INTER_HSPACE, mlenmsg, 1);
    r_set(&bt, 1+INTER_WSPACE, 2+INTER_HSPACE+nummsg, mlenbut, 1);
    r_set(&ic, 0, 0, 0, 0);

    /*
     * if we have an icon, we must initialise 'ic' and adjust:
     *  the width of the alert
     *  the horizontal position of the first message line.
     * since the alert at this stage is sized in characters, we must
     * convert the icon height from pixels to characters, based on
     * the current character height.
     */
    if (iconnum)
    {
        hicon = divs(rs_bitblk[NOTEBB].bi_hl+gl_hchar-1, gl_hchar);
        r_set(&ic, 1+INTER_WSPACE, 1+INTER_HSPACE, 4, hicon);
        al.g_w += ic.g_w + 1 + INTER_WSPACE;
        ms.g_x = ic.g_x + ic.g_w + 1 + INTER_WSPACE;
    }

    /*
     * final adjustments(1): alert width / button horizontal position
     *  if the message lines need more space than the buttons, set the
     *  alert width from the message length, and adjust the horizontal
     *  position of the first button for a symmetrical effect.
     *  otherwise, set the alert width from the button sizes and leave
     *  the message lines left justified.
     */
    allbut = numbut * mlenbut + 2*(numbut-1);
    if (mlenmsg + al.g_w > allbut + 1 + INTER_WSPACE)
    {
        al.g_w += mlenmsg + 1 + INTER_WSPACE;
        bt.g_x = (al.g_w - allbut) / 2;
    }
    else
    {
        al.g_w = allbut + 2 * (1+INTER_WSPACE);
        bt.g_x = 1 + INTER_WSPACE;
    }

    /*
     * final adjustments(2): button vertical position / alert height
     *  ensure no overlap by putting the buttons below the icon,
     *  subject to leaving at least a 1-line gap between the messages
     *  and the buttons.
     */
    bt.g_y = max(ic.g_y+ic.g_h,nummsg+1) + 1 + INTER_HSPACE;
    al.g_h = max(bt.g_y+bt.g_h,ic.g_y+ic.g_h) + 1 + INTER_HSPACE;

    /* init. root object    */
    ob_setxywh(tree, ROOT, &al);
    for (i = 0, obj = tree; i < NUM_ALOBJS; i++, obj++)
          obj->ob_next = obj->ob_head = obj->ob_tail = -1;

    /* add icon object      */
    if (iconnum)
    {
        switch(iconnum) {
        case 1:
            i = NOTEBB;
            break;
        case 2:
            i = QUESTBB;
            break;
        default:
            i = STOPBB;
            break;
        }
        obj = tree + 1;
        obj->ob_spec.bitblk = CONST_CAST(BITBLK *, &rs_bitblk[i]);
        ob_setxywh(tree, 1, &ic);
        ob_add(tree, ROOT, 1);
    }

    /* add msg objects      */
    for (i = 0; i < nummsg; i++)
    {
        ob_setxywh(tree, MSGOFF+i, &ms);
        ms.g_y++;
        ob_add(tree, ROOT, MSGOFF+i);
    }

    /* add button objects with 1 space between them  */
    for (i = 0, obj = tree+BUTOFF; i < numbut; i++, obj++)
    {
        obj->ob_flags = BUTTON_FLAGS;
        obj->ob_state = NORMAL;
        ob_setxywh(tree, BUTOFF+i, &bt);
        bt.g_x += mlenbut + 2;
        ob_add(tree, ROOT, BUTOFF+i);
    }

    /* set last object flag */
    (--obj)->ob_flags |= LASTOB;

    /* convert to pixels    */
    for (i = 0; i < NUM_ALOBJS; i++)
        rs_obfix(tree, i);

    /*
     * final pixel adjustments for 3D objects
     *  (1) increase button height by 3 pixels, which adds an extra pixel
     *      between the top of the text and the button outline.  this
     *      provides slightly better aesthetics and is what TOS4 does
     *  (2) move the buttons upwards to allow for this
     */
#if ALERT_3D
    for (i = 0, obj = tree+BUTOFF; i < numbut; i++, obj++)
    {
        obj->ob_y -= ADJ3DSTD;
        obj->ob_height += 2 * ADJ3DSTD - 1;
    }
#endif
}


OBJECT *fm_alert_tree(WORD defbut, char *palstr, GRECT *rectangle)
{
    WORD inm, nummsg, mlenmsg, numbut, mlenbut;
    OBJECT *tree = rs_trees[DIALERT];
    OBJECT *obj;

    fm_parse(tree, palstr, &inm, &nummsg, &mlenmsg, &numbut, &mlenbut);
    fm_build(tree, inm, nummsg, mlenmsg, numbut, mlenbut);

    if (defbut > 0 && defbut <= numbut)
    {
        obj = tree + BUTOFF + defbut - 1;
        obj->ob_flags |= DEFAULT;
    }

    obj = tree + 1;

    if (inm != 0)
    {
        WORD image;

        switch(inm) {
        case 1:
            image = NOTEBB;
            break;
        case 2:
            image = QUESTBB;
            break;
        default:
            image = STOPBB;
            break;
        }
        obj->ob_spec.bitblk = CONST_CAST(BITBLK *, &rs_bitblk[image]);
    }

    /* fix up icon, 32x32   */
    obj = tree + 1;
    obj->ob_type = G_IMAGE;
    obj->ob_width = obj->ob_height = 32;

    /* center tree on screen, limiting drawing to the screen */
    ob_center(tree, rectangle);
    rc_intersect(&gl_rscreen, rectangle);
    return tree;
}

