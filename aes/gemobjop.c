/*      GEMOBJOP.C      03/15/84 - 05/27/85     Gregg Morris            */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */
/*      fix get_par                             11/12/87        mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2022 The EmuTOS development team
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

#include "emutos.h"
#include "obdefs.h"
#include "gemobjop.h"
#include "aesdefs.h"
#include "gemoblib.h"
#include "gemgraf.h"
#include "string.h"


#include "gemobwalk.inc"


#if !CONF_WITH_LIBAES
/*
 * Routine that will find the parent of a given object.  The
 * idea is to walk to the end of our siblings and return
 * our parent.  If object is the root then return NIL as parent.
 */
WORD get_par(OBJECT *tree, WORD obj)
{
    WORD next;

    if (obj == ROOT)
        return NIL;

    while(1)
    {
        next = tree[obj].ob_next;
        /* if the next object's tail points to us, it must be our parent */
        if (tree[next].ob_tail == obj)
            break;
        obj = next;
    }

    return next;
}

/*
 *  Routine to find the x,y offset of a particular object relative
 *  to the physical screen.  This involves accumulating the offsets
 *  of all the object's parents up to and including the root.
 */
void ob_offset(OBJECT *tree, WORD obj, WORD *pxoff, WORD *pyoff)
{
    OBJECT *treeptr = tree;

    *pxoff = *pyoff = 0;
    do
    {
        *pxoff += (treeptr+obj)->ob_x;  /* add in current object's offsets */
        *pyoff += (treeptr+obj)->ob_y;
        obj = get_par(tree, obj);       /* then get parent */
    } while (obj != NIL);
}


#endif

/*
 *  ob_relxywh: fill GRECT with x/y/w/h from object (relative x/y)
 */
void ob_relxywh(OBJECT *tree, WORD obj, GRECT *pt)
{
    OBJECT *objptr = tree + obj;

    memcpy(pt, &objptr->ob_x, sizeof(GRECT));
}


/*
 *  ob_actxywh: fill GRECT with x/y/w/h of object (absolute x/y)
 */
void ob_actxywh(OBJECT *tree, WORD obj, GRECT *pt)
{
#if CONF_WITH_3D_OBJECTS
    OBSPEC spec;
    WORD state, type, flags, border, adjust;

    ob_offset(tree, obj, &pt->g_x, &pt->g_y);
    ob_sst(tree, obj, &spec, &state, &type, &flags, pt, &border);

    /* if 3D object, adjust position & size */
    if (flags & FL3DOBJ)
    {
        adjust = -ADJ3DSTD;
        if (state & OUTLINED)
            adjust -= ADJ3DOUT;
        gr_inside(pt, adjust);
        if ((state & SHADOWED) && (border < 0))
        {
            adjust = border * ADJ3DSHA;
            pt->g_w -= adjust;
            pt->g_h -= adjust;
        }
    }
#else
    OBJECT *objptr = tree + obj;

    ob_offset(tree, obj, &pt->g_x, &pt->g_y);
    pt->g_w = objptr->ob_width;
    pt->g_h = objptr->ob_height;
#endif
}


/*
 * ob_setxywh: copy values from GRECT into object
 */
void ob_setxywh(OBJECT *tree, WORD obj, GRECT *pt)
{
    OBJECT *objptr = tree + obj;

    memcpy(&objptr->ob_x, pt, sizeof(GRECT));
}
