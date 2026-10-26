/*      GEMOBLIB.C      03/15/84 - 05/27/85     Gregg Morris            */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2025 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "struct.h"
#include "obdefs.h"
#include "aesext.h"
#include "gemlib.h"
#include "funcdef.h"

#include "geminit.h"
#include "gemgsxif.h"
#include "gemobjop.h"
#if CONF_WITH_LIBAES
#include "gemobjtree.h"
#endif
#include "gemgraf.h"
#include "optimopt.h"
#include "rectfunc.h"
#include "intmath.h"
#include "gemoblib.h"

#include "string.h"
#if ARCH_X86_64
#include "aes_objects_kernel.h"
#define OB_RENDER_CANCELLED(tree) x86_64_aes_tree_cancelled(tree)
#else
#define OB_RENDER_CANCELLED(tree) FALSE
#endif


#include "gemobdraw.inc"


/*
 *  Object draw routine that walks tree and draws appropriate objects.
 */
void ob_draw(OBJECT *tree, WORD obj, WORD depth)
{
    WORD pobj;
    WORD last = NIL;
    WORD sx, sy;
#if ARCH_X86_64
    AES_TREE_STATE *guard = x86_64_aes_tree_pin(tree);
    GRECT saved, bounded;
#endif

#if ARCH_X86_64
    if (guard) {
        gsx_gclip(&saved);
        bounded = saved.g_w && saved.g_h ? saved : gl_rscreen;
        if (!rc_intersect(&gl_rscreen, &bounded)) {
            x86_64_aes_tree_unpin(guard);
            return;
        }
        gsx_sclip(&bounded);
    }
#endif
    if (obj != ROOT)
        last = tree[obj].ob_next;
    pobj = get_par(tree, obj);

    if (pobj != NIL)
        ob_offset(tree, pobj, &sx, &sy);
    else
        sx = sy = 0;

    gsx_moff();
    everyobj(tree, obj, last, just_draw, sx, sy, depth);
    gsx_mon();
#if ARCH_X86_64
    if (guard)
        gsx_sclip(&saved);
    x86_64_aes_tree_unpin(guard);
#endif
}


/*
 *  Routine to find the object that is previous to us in the
 *  tree.  The idea is we get our parent and then walk down
 *  his list of children until we find a sibling who points to
 *  us.  If we are the first child or we have no parent then
 *  return NIL.
 */
static WORD get_prev(OBJECT *tree, WORD parent, WORD obj)
{
    WORD nobj, pobj;
    OBJECT *treeptr = tree;

    pobj = (treeptr+parent)->ob_head;
    if (pobj == obj)
        return NIL;

    while(TRUE)
    {
        nobj = (treeptr+pobj)->ob_next;
        if (nobj == obj)        /* next object is us,         */
            return pobj;        /*  so return previous object */

        if (nobj == parent)     /* next object is the parent,  */
            return NIL;         /*  so we're not in this chain */

        pobj = nobj;
    }
}


/*
 *  ob_find: routine to find out which object a certain mx,my value is over
 *
 *  Since each parent object contains its children the idea is to
 *  walk down the tree, limited by the depth parameter, and find
 *  the deepest object the mx,my location was over.
 *
 *  A note on ob_find() with 3D objects
 *  ===================================
 *  With non-3D objects, in a properly-constructed resource, a child
 *  object will always have its x coordinate greater than or equal to
 *  the x coordinate of the parent, and ob_find() relies on this.
 *  Since 3D objects may be expanded visually, this principle may no
 *  longer be true in certain circumstances, causing ob_find() to fail.
 *
 *  For example, consider an i-box surrounding a set of radio buttons,
 *  where the i-box has the same x coordinate as the leftmost button.
 *  The buttons will be expanded when displayed but the i-box will not.
 *  Then, if the user clicks just inside the leftmost radio button, mx/my
 *  may NOT be inside the i-box.  In this case, ob_find() will not find
 *  the radio button object, and the click will (probably) be ignored.
 *
 *  This issue also occurs with Atari TOS 4.
 */
WORD ob_find(OBJECT *tree, WORD currobj, WORD depth, WORD mx, WORD my)
{
    WORD lastfound;
    WORD dosibs, done;
    GRECT t, o;
    WORD parent, childobj, flags;
    OBJECT *objptr;

    lastfound = NIL;

    if (currobj == ROOT)
        r_set(&o, 0, 0, 0, 0);
    else
    {
        parent = get_par(tree, currobj);
        ob_actxywh(tree, parent, &o);
    }

    done = FALSE;
    dosibs = FALSE;

    while(!done)
    {
        /*
         * if inside this obj, might be inside a child, so check
         */
        objptr = tree + currobj;

#if CONF_WITH_3D_OBJECTS
        if (objptr->ob_state & SHADOWED)
        {
            ob_relxywh(tree, currobj, &t);
            t.g_x += o.g_x;
            t.g_y += o.g_y;
        }
        else
        {
            ob_actxywh(tree, currobj, &t);
        }
#else
        ob_relxywh(tree, currobj, &t);
        t.g_x += o.g_x;
        t.g_y += o.g_y;
#endif

        flags = objptr->ob_flags;
        if ( (inside(mx, my, &t)) && (!(flags & HIDETREE)) )
        {
            lastfound = currobj;

            childobj = objptr->ob_tail;
            if ((childobj != NIL) && depth)
            {
                currobj = childobj;
                depth--;
                o.g_x = t.g_x;
                o.g_y = t.g_y;
                dosibs = TRUE;
            }
            else
                done = TRUE;
        }
        else
        {
            if (dosibs && (lastfound != NIL))
            {
                currobj = get_prev(tree, lastfound, currobj);
                if (currobj == NIL)
                    done = TRUE;
            }
            else
                done = TRUE;
        }
    }

    /*
     * if no one was found, this will return NIL
     */
    return lastfound;
}


#if CONF_WITH_LIBAES
/* The external-library feature selects the pure shared core. Keep the
 * legacy kernel diagnostic here, outside that application-local code. */
WORD ob_delete(OBJECT *tree, WORD obj)
{
    WORD result = ob_delete_local(tree, obj);
    if (!result && obj != ROOT)
        KINFO(("objc_delete() failed: object %d not found\n", obj));
    return result;
}
#else
/*
 *  Routine to add a child object to a parent object.  The child
 *  is added at the end of the parent's current sibling list.
 *  It is also initialized.
 */
void ob_add(OBJECT *tree, WORD parent, WORD child)
{
    WORD lastkid;
    OBJECT *treeptr = tree;
    OBJECT *parentptr;

    if ((parent != NIL) && (child != NIL))
    {
        parentptr = treeptr + parent;

        /* initialize child */
        (treeptr+child)->ob_next = parent;

        lastkid = parentptr->ob_tail;
        if (lastkid == NIL)             /* this is parent's 1st kid, so */
            parentptr->ob_head = child; /* both head and tail point to it  */
        else
            (treeptr+lastkid)->ob_next = child; /* add kid to end of kid list */
        parentptr->ob_tail = child;
    }
}


/*
 *  Routine to delete an object from the tree.
 */
WORD ob_delete(OBJECT *tree, WORD obj)
{
    WORD parent;
    WORD prev, nextsib;
    OBJECT *treeptr = tree;
    OBJECT *parentptr, *prevptr;

    if (obj == ROOT)
        return 0;           /* can't delete the root object! */

    nextsib = tree[obj].ob_next;
    parent = get_par(tree, obj);

    parentptr = treeptr + parent;

    if (parentptr->ob_head == obj)      /* this is head child in list */
    {
        if (parentptr->ob_tail == obj)
        {
            /*
             * this is the only child in the list,
             * so fix head & tail ptrs
             */
            nextsib = NIL;
            parentptr->ob_tail = NIL;
        }

        /* move head ptr to next child in list */
        parentptr->ob_head = nextsib;
    }
    else                /* it's somewhere else, so move pnext around it */
    {
        prev = get_prev(tree, parent, obj);
        if (prev == NIL)  /* object not found */
        {
            KINFO(("objc_delete() failed: object %d not found\n",obj));
            return 0;
        }

        prevptr = treeptr + prev;
        prevptr->ob_next = nextsib;
        if (parentptr->ob_tail == obj)  /* this is last child in list, so move */
            parentptr->ob_tail = prev;  /*  tail ptr to previous child in list */
    }

    return 1;
}


/*
 *  Routine to change the order of an object relative to its
 *  siblings in the tree.  0 is the head of the list and NIL
 *  is the tail of the list.
 */
BOOL ob_order(OBJECT *tree, WORD mov_obj, WORD new_pos)
{
    WORD parent;
    WORD chg_obj, ii;
    OBJECT *treeptr = tree;
    OBJECT *parentptr, *movptr, *chgptr;

    if (mov_obj == ROOT)
        return FALSE;

    parent = get_par(tree, mov_obj);
    parentptr = treeptr + parent;
    movptr = treeptr + mov_obj;

    ob_delete(tree, mov_obj);
    chg_obj = parentptr->ob_head;
    if (new_pos == 0)
    {
          movptr->ob_next = chg_obj;    /* put mov_obj at head of list */
          parentptr->ob_head = mov_obj;
    }
    else
    {
        /* find new_pos */
        if (new_pos == NIL)
            chg_obj = parentptr->ob_tail;
        else
        {
            for (ii = 1; ii < new_pos; ii++)
            {
                chgptr = treeptr + chg_obj;
                chg_obj = chgptr->ob_next;
            }
        }

        /* now add mov_obj after chg_obj */
        chgptr = treeptr + chg_obj;
        movptr->ob_next = chgptr->ob_next;
        chgptr->ob_next = mov_obj;
    }

    if (movptr->ob_next == parent)
        parentptr->ob_tail = mov_obj;

    return TRUE;
}


#endif

/*
 *  Routine to change the state of an object and redraw that
 *  object using the current clip rectangle.
 */
#include "gemobchange.inc"

void ob_change(OBJECT *tree, WORD obj, UWORD new_state, WORD redraw)
{
#if ARCH_X86_64
    AES_TREE_STATE *guard = x86_64_aes_tree_pin(tree);
    GRECT saved, bounded;

    if (guard) {
        gsx_gclip(&saved);
        bounded = saved.g_w && saved.g_h ? saved : gl_rscreen;
        if (!rc_intersect(&gl_rscreen, &bounded))
            redraw = FALSE;
        else
            gsx_sclip(&bounded);
    }
#endif
    ob_change_native(tree, obj, new_state, redraw);
#if ARCH_X86_64
    if (guard)
        gsx_sclip(&saved);
    x86_64_aes_tree_unpin(guard);
#endif
}


#if CONF_WITH_3D_OBJECTS
/*
 *  Initialise variables related to 3D objects
 */
void init_3d(void)
{
    indtxtmove = 0;         /* indicators: don't move text */
    indcolchange = 1;       /* ... but change colour       */

    acttxtmove = 1;         /* activators: move text       */
    actcolchange = 0;       /* ... but don't change colour */

    /* initialise button colours according to size of palette */
    if (gl_ws.ws_ncolors <= LWHITE)
    {
        actbutcol = indbutcol = backgrcol = WHITE;
    }
    else
    {
        actbutcol = indbutcol = backgrcol = LWHITE;
    }
}


/*
 *  Implements objc_sysvar(): set/get variables to control 3D object display
 */
WORD ob_sysvar(WORD mode, WORD which, WORD in1, WORD in2, WORD *out1, WORD *out2)
{
    /*
     * handle setting the variables
     */
    if (mode)
    {
        switch(which) {
        case LK3DIND:
            if (in1 != -1)
                indtxtmove = in1;
            if (in2 != -1)
                indcolchange = in2;
            break;
        case LK3DACT:
            if (in1 != -1)
                acttxtmove = in1;
            if (in2 != -1)
                actcolchange = in2;
            break;
        case INDBUTCOL:
            if (in1 >= gl_ws.ws_ncolors)
                return 0;
            indbutcol = in1;
            break;
        case ACTBUTCOL:
            if (in1 >= gl_ws.ws_ncolors)
                return 0;
            actbutcol = in1;
            break;
        case BACKGRCOL:
            if (in1 >= gl_ws.ws_ncolors)
                return 0;
            backgrcol = in1;
            break;
        default:
            return 0;
            break;
        }
        return 1;
    }

    /*
     * handle getting the variables
     */
    switch(which) {
    case LK3DIND:
        *out1 = indtxtmove;
        *out2 = indcolchange;
        break;
    case LK3DACT:
        *out1 = acttxtmove;
        *out2 = actcolchange;
        break;
    case INDBUTCOL:
        *out1 = indbutcol;
        break;
    case ACTBUTCOL:
        *out1 = actbutcol;
        break;
    case BACKGRCOL:
        *out1 = backgrcol;
        break;
    case AD3DVALUE:
        *out1 = ADJ3DSTD;
        *out2 = ADJ3DSTD;
        break;
    default:
        return 0;
        break;
    }

    return 1;
}
#endif


UWORD ob_fs(OBJECT *tree, WORD obj, WORD *pflag)
{
    OBJECT *objptr = tree + obj;

    *pflag = objptr->ob_flags;
    return objptr->ob_state;
}
