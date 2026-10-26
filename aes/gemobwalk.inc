/* Shared original AES implementation; included by native AES and libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
BYTE ob_sst(OBJECT *tree, WORD obj, OBSPEC *pspec, WORD *pstate, WORD *ptype,
            WORD *pflags, GRECT *pt, WORD *pth)
{
    WORD    th;
    OBJECT  *objptr = tree + obj;
    TEDINFO *ted;

    pt->g_w = objptr->ob_width;
    pt->g_h = objptr->ob_height;
    *pflags = objptr->ob_flags;
    *pspec = objptr->ob_spec;
    if (objptr->ob_flags & INDIRECT)
        *pspec = *(objptr->ob_spec.indirect);

    *pstate = objptr->ob_state;
    *ptype = objptr->ob_type & 0x00ff;
    th = 0;
    switch(*ptype)
    {
    case G_TITLE:
        th = 1;
        break;
    case G_TEXT:
    case G_BOXTEXT:
    case G_FTEXT:
    case G_FBOXTEXT:
        ted = pspec->tedinfo;
        th = ted->te_thickness;
        break;
    case G_BOX:
    case G_BOXCHAR:
    case G_IBOX:
        th = pspec->obspec.framesize;
        break;
    case G_BUTTON:
        th--;
        if (objptr->ob_flags & EXIT)
            th--;
        if (objptr->ob_flags & DEFAULT)
            th--;
        break;
    }

    if (th > 128)
        th -= 256;
    *pth = th;

    return pspec->obspec.character;  /* only useful for G_BOXCHAR */
}


void everyobj(OBJECT *tree, WORD this, WORD last, EVERYOBJ_CALLBACK routine,
              WORD startx, WORD starty, WORD maxdep)
{
    WORD    tmp1;
    WORD    depth;
    WORD    x[MAX_DEPTH+2], y[MAX_DEPTH+2];
    OBJECT  *obj;

    x[0] = startx;
    y[0] = starty;
    depth = 1;

    /*
     * non-recursive tree traversal
     */
child:
    /* see if we need to stop */
    if (this == last)
        return;

    /* do this object */
    obj = tree + this;
    x[depth] = x[depth-1] + obj->ob_x;
    y[depth] = y[depth-1] + obj->ob_y;
    (*routine)(tree, this, x[depth], y[depth]);

    /* if this guy has kids then do them */
    tmp1 = obj->ob_head;
    if (tmp1 != NIL)
    {
        if (!(obj->ob_flags & HIDETREE) && (depth <= maxdep))
        {
            depth++;
            this = tmp1;
            goto child;
        }
    }

sibling:
    /*
     * if this is the root (which has no parent),
     *  or it is the last then stop else...
     */
    obj = tree + this;
    tmp1 = obj->ob_next;
    if ((tmp1 == last) || (this == ROOT))
        return;
    /*
     * if this object has a sibling that is not his parent,
     * then move to him and do him and his kids
     */
    obj = tree + tmp1;
    if (obj->ob_tail != this)
    {
        this = tmp1;
        goto child;
    }
    /* else move up to the parent and finish off his siblings */
    depth--;
    this = tmp1;
    goto sibling;
}
