/* Pure object-tree editing and coordinates, shared by kernel AES/libaes.
 * Extracted from gemobjop.c and gemoblib.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later.
 */
#include "emutos.h"
#include "obdefs.h"
#include "gemobjtree.h"

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
WORD ob_delete_local(OBJECT *tree, WORD obj)
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

    if (mov_obj <= ROOT || new_pos < NIL)
        return FALSE;

    parent = get_par(tree, mov_obj);
    parentptr = treeptr + parent;
    movptr = treeptr + mov_obj;

    /* Reject an invalid position before changing any links. */
    if (new_pos != NIL)
    {
        chg_obj = parentptr->ob_head;
        for (ii = 0; chg_obj != parent; ii++)
            chg_obj = treeptr[chg_obj].ob_next;
        if (new_pos >= ii)
            return FALSE;
    }

    if (!ob_delete_local(tree, mov_obj))
        return FALSE;
    /* Deleting an only child empties the list. Both head and tail must
     * be restored, for position zero as well as the tail sentinel. */
    if (parentptr->ob_head == NIL)
    {
        ob_add(tree, parent, mov_obj);
        return TRUE;
    }
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
