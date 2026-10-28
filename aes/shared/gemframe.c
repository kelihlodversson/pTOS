/* Native GEM 3D window construction, extracted from gemwmlib.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemframe.h"
#include "aesdefs.h"
#include "intmath.h"
#include "string.h"
static const WORD gl_watype[NUM_ELEM] =
{
    G_IBOX,         /* W_BOX        */
    G_BOX,          /* W_TITLE      */
    G_BOXCHAR,      /* W_CLOSER     */
    G_BOXTEXT,      /* W_NAME       */
    G_BOXCHAR,      /* W_FULLER     */
    G_BOXTEXT,      /* W_INFO       */
    G_IBOX,         /* W_DATA       */
    G_IBOX,         /* W_WORK       */
    G_BOXCHAR,      /* W_SIZER      */
    G_BOX,          /* W_VBAR       */
    G_BOXCHAR,      /* W_UPARROW    */
    G_BOXCHAR,      /* W_DNARROW    */
    G_BOX,          /* W_VSLIDE     */
    G_BOX,          /* W_VELEV      */
    G_BOX,          /* W_HBAR       */
    G_BOXCHAR,      /* W_LFARROW    */
    G_BOXCHAR,      /* W_RTARROW    */
    G_BOX,          /* W_HSLIDE     */
    G_BOX           /* W_HELEV      */
};

static const LONG gl_waspec[NUM_ELEM] =
{
    0x00011101L,    /* W_BOX        */
    0x00011101L,    /* W_TITLE      */
    0x05011101L,    /* W_CLOSER     */
    0x0L,           /* W_NAME       */
    0x07011101L,    /* W_FULLER     */
    0x0L,           /* W_INFO       */
    0x00001101L,    /* W_DATA       */
    0x00001101L,    /* W_WORK       */
    0x06011101L,    /* W_SIZER      */
    0x00011101L,    /* W_VBAR       */
    0x01011101L,    /* W_UPARROW    */
    0x02011101L,    /* W_DNARROW    */
    0x00011111L,    /* W_VSLIDE     */
    0x00011101L,    /* W_VELEV      */
    0x00011101L,    /* W_HBAR       */
    0x04011101L,    /* W_LFARROW    */
    0x03011101L,    /* W_RTARROW    */
    0x00011111L,    /* W_HSLIDE     */
    0x00011101L     /* W_HELEV      */
};

static const WORD gl_3dflags[NUM_ELEM] =
{
    FL3DNONE,       /* W_BOX        */
    FL3DNONE,       /* W_TITLE      */
    FL3DACT,        /* W_CLOSER     */
    FL3DNONE,       /* W_NAME - may be altered by w_bldactive() */
    FL3DACT,        /* W_FULLER     */
    FL3DNONE,       /* W_INFO       */
    FL3DNONE,       /* W_DATA       */
    FL3DNONE,       /* W_WORK       */
    FL3DACT,        /* W_SIZER      */
    FL3DNONE,       /* W_VBAR       */
    FL3DACT,        /* W_UPARROW    */
    FL3DACT,        /* W_DNARROW    */
    FL3DNONE,       /* W_VSLIDE     */
    FL3DACT,        /* W_VELEV      */
    FL3DNONE,       /* W_HBAR       */
    FL3DACT,        /* W_LFARROW    */
    FL3DACT,        /* W_RTARROW    */
    FL3DNONE,       /* W_HSLIDE     */
    FL3DACT         /* W_HELEV      */
};


static char empty_name[] = "";
static const TEDINFO text_template = {
    NULL,NULL,NULL,IBM,0,TE_LEFT,0x1100,0,1,80,80
};
void wm_frame_init(WM_FRAME *frame)
{
    WORD i;
    memset(frame,0,sizeof(*frame));
    for (i = 0; i < NUM_ELEM; i++) {
        frame->tree[i].ob_next = frame->tree[i].ob_head = frame->tree[i].ob_tail = NIL;
        frame->tree[i].ob_type = gl_watype[i];
        frame->tree[i].ob_spec.index = gl_waspec[i];
        frame->tree[i].ob_flags = gl_3dflags[i];
    }
    frame->tree[ROOT].ob_state = SHADOWED;
    frame->name = frame->info = text_template;
    frame->name.te_just = TE_CNTR;
    frame->tree[W_NAME].ob_spec.tedinfo = &frame->name;
    frame->tree[W_INFO].ob_spec.tedinfo = &frame->info;
}
static void frame_adjust(OBJECT *tree, WORD parent, WORD obj, WORD x, WORD y, WORD w, WORD h)
{
    WORD last = tree[parent].ob_tail;
    tree[obj].ob_x = x; tree[obj].ob_y = y;
    tree[obj].ob_width = w; tree[obj].ob_height = h;
    tree[obj].ob_head = tree[obj].ob_tail = NIL;
    tree[obj].ob_next = parent;
    if (last == NIL) tree[parent].ob_head = obj;
    else tree[last].ob_next = obj;
    tree[parent].ob_tail = obj;
}
static void frame_colour(OBJECT *tree, const WM_FRAME_STATE *state, WORD gadget)
{
    const WORD *colours = state->topped ? state->top_colours : state->bottom_colours;
    WORD colour;
    if (!colours) return;
    colour = colours[gadget];
    if ((tree[gadget].ob_type & 255) == G_BOXTEXT) tree[gadget].ob_spec.tedinfo->te_color = colour;
    else tree[gadget].ob_spec.index = (tree[gadget].ob_spec.index & 0xffff0000UL) | (UWORD)colour;
}

#define TGADGETS (NAME|CLOSER|FULLER|MOVER)
#define VGADGETS (UPARROW|DNARROW|VSLIDE)
#define HGADGETS (LFARROW|RTARROW|HSLIDE)
static void frame_vbar(OBJECT *tree, const WM_FRAME_STATE *state, UWORD kind, WORD x, WORD y, WORD w, WORD h)
{
    WORD size, posn;
    (void)w;

    frame_colour(tree, state, W_VBAR);
    frame_adjust(tree, W_DATA, W_VBAR, x, y, state->adjusted_width, h);

    y = 0;

    if (kind & UPARROW)
    {
        frame_colour(tree, state, W_UPARROW);
        frame_adjust(tree, W_VBAR, W_UPARROW, ADJ3DSTD, y+ADJ3DSTD, state->wbox, state->hbox);
        y += state->adjusted_height;      /* adjust y for VSLIDE */
        h -= state->adjusted_height;      /* adjust h for VSLIDE */
    }

    if (kind & DNARROW)
    {
        h -= state->adjusted_height;      /* adjust h for DNARROW, VSLIDE */
        frame_colour(tree, state, W_DNARROW);
        frame_adjust(tree, W_VBAR, W_DNARROW, ADJ3DSTD, y+h+ADJ3DSTD-1, state->wbox, state->hbox);
    }

    if (kind & VSLIDE)
    {
        frame_colour(tree, state, W_VSLIDE);
        frame_adjust(tree, W_VBAR, W_VSLIDE, 0, y, state->adjusted_width, h);
        if (state->vslsiz == -1)
            size = state->hbox;
        else size = max(state->hbox, mul_div(h, state->vslsiz, 1000));
        posn = mul_div(h-size, state->vslide, 1000);
        frame_colour(tree, state, W_VELEV);
        frame_adjust(tree, W_VSLIDE, W_VELEV, ADJ3DSTD, posn+ADJ3DSTD, state->wbox, size-2*ADJ3DSTD-1);
    }
}


static void frame_hbar(OBJECT *tree, const WM_FRAME_STATE *state, UWORD kind, WORD x, WORD y, WORD w, WORD h)
{
    WORD size, posn;
    (void)h;

    frame_colour(tree, state, W_HBAR);
    frame_adjust(tree, W_DATA, W_HBAR, x, y, w, state->adjusted_height);

    x = 0;

    if (kind & LFARROW)
    {
        frame_colour(tree, state, W_LFARROW);
        frame_adjust(tree, W_HBAR, W_LFARROW, x+ADJ3DSTD, ADJ3DSTD, state->wbox, state->hbox);
        x += state->adjusted_width;      /* adjust x for HSLIDE */
        w -= state->adjusted_width;      /* adjust w for HSLIDE */
    }

    if (kind & RTARROW)
    {
        w -= state->adjusted_width;      /* adjust w for RTARROW, HSLIDE */
        frame_colour(tree, state, W_RTARROW);
        frame_adjust(tree, W_HBAR, W_RTARROW, x+w-1+ADJ3DSTD, ADJ3DSTD, state->wbox, state->hbox);
    }

    if (kind & HSLIDE)
    {
        frame_colour(tree, state, W_HSLIDE);
        frame_adjust(tree, W_HBAR, W_HSLIDE, x, 0, w, state->adjusted_height);
        if (state->hslsiz == -1)
            size = state->wbox;
        else size = max(state->wbox, mul_div(w, state->hslsiz, 1000));
        posn = mul_div(w-size, state->hslide, 1000);
        frame_colour(tree, state, W_HELEV);
        frame_adjust(tree, W_HSLIDE, W_HELEV, posn+ADJ3DSTD, ADJ3DSTD, size-2*ADJ3DSTD-1, state->hbox);
    }
}


void wm_frame_build(OBJECT *tree, TEDINFO *name, TEDINFO *info, const WM_FRAME_STATE *state)
{
    BOOL istop = state->topped, havevbar, havehbar;
    UWORD kind = state->kind;
    WORD corner_x, corner_y, tempw, i;
    GRECT t;
    for (i = 0; i < NUM_ELEM; i++)
        tree[i].ob_next = tree[i].ob_head = tree[i].ob_tail = NIL;
    /* start adding pieces & adjusting sizes */
    name->te_ptext = (kind & NAME) ? state->name : (char *)empty_name;
    info->te_ptext = state->info;
    name->te_just = TE_CNTR;

    t = state->border;
    tree[W_BOX].ob_x = t.g_x;
    tree[W_BOX].ob_y = t.g_y;
    tree[W_BOX].ob_width = t.g_w;
    tree[W_BOX].ob_height = t.g_h;
    frame_colour(tree, state, W_BOX);

    /* do title area */
    t.g_x = t.g_y = 0;
    if (kind & TGADGETS)
    {
        frame_colour(tree, state, W_TITLE);
        frame_adjust(tree, W_BOX, W_TITLE, t.g_x, t.g_y, t.g_w, state->adjusted_height);
        tempw = t.g_w;
        if (kind & CLOSER)
        {
            frame_colour(tree, state, W_CLOSER);
            frame_adjust(tree, W_TITLE, W_CLOSER, t.g_x+ADJ3DSTD, t.g_y+ADJ3DSTD, state->wbox, state->hbox);
            t.g_x += state->adjusted_width;
            tempw -= state->adjusted_width;
        }
        if (kind & FULLER)
        {
            frame_colour(tree, state, W_FULLER);
            tempw -= state->adjusted_width;
            frame_adjust(tree, W_TITLE, W_FULLER, t.g_x+tempw+ADJ3DSTD, t.g_y+ADJ3DSTD, state->wbox, state->hbox);
        }
        if (kind & NAME)
        {
            frame_colour(tree, state, W_NAME);
            tempw -= 2 * ADJ3DSTD;
            frame_adjust(tree, W_TITLE, W_NAME, t.g_x+ADJ3DSTD, t.g_y+ADJ3DSTD, tempw, state->hbox);
            tree[W_NAME].ob_state = istop ? NORMAL : DISABLED;
        if (!state->top_colours)
            name->te_color = istop ? 0x11a1 : 0x1100;

        }
        else if (kind & MOVER)
        {
            /*
             * in TOS4, you may specify a MOVER gadget with no NAME gadget
             */
            tempw -= 2 * ADJ3DSTD;
            frame_adjust(tree, W_TITLE, W_NAME, t.g_x+ADJ3DSTD, t.g_y+ADJ3DSTD, tempw, state->hbox);
            tree[W_NAME].ob_state = istop ? NORMAL : DISABLED;
            name->te_color = 0;
        }

        tree[W_NAME].ob_flags = (kind & NAME) ? FL3DACT : FL3DNONE;

        t.g_x = 0;
        t.g_y += state->adjusted_height;
        t.g_h -= state->adjusted_height;
    }

    /* do info area */
    if (kind & INFO)
    {
        frame_colour(tree, state, W_INFO);
        frame_adjust(tree, W_BOX, W_INFO, t.g_x, t.g_y, t.g_w, state->hbox);
        t.g_y += state->hbox;
        t.g_h -= state->hbox;
    }

    /* do data area */
    frame_adjust(tree, W_BOX, W_DATA, t.g_x, t.g_y, t.g_w, t.g_h);

    /* determine whether we should display sizer & vertical/horizontal bars */
    havevbar = (kind & VGADGETS) ? TRUE : FALSE;
    havehbar = (kind & HGADGETS) ? TRUE : FALSE;

    /*
     * If we have a SIZER, we must draw a vertical bar and/or a horizontal
     * bar.  Atari TOS versions 1-3 always drew both, but TOS4 only draws
     * the one(s) containing other window elements.  If no other window
     * elements are present, it draws a vertical bar.  We do the same.
     */

    /* if we have both bars or a SIZER, we draw a corner element */
    corner_x = corner_y = -1;   /* assume no corner (SIZER or dummy) */
    if ((havehbar && havevbar) || (kind & SIZER))
    {
        corner_x = t.g_w - state->wbox - ADJ3DSTD;
        corner_y = t.g_h - state->hbox - ADJ3DSTD;
    }
    /* if we have a SIZER but no bars, we draw a vertical bar */
    if ((!havevbar && !havehbar) && (kind & SIZER))
        havevbar = TRUE;

    /* do work area */
    t.g_x = t.g_y = 1;      /* allow a 1-pixel border */
    t.g_w -= 2;
    t.g_h -= 2;
    if (havevbar || (kind & SIZER))
        t.g_w -= (state->adjusted_width - 1);
    if (havehbar || (kind & SIZER))
        t.g_h -= (state->adjusted_height - 1);

    frame_adjust(tree, W_DATA, W_WORK, t.g_x, t.g_y, t.g_w, t.g_h);

    /* do vertical bar area */
    if (havevbar)
    {
        t.g_x += t.g_w;
        frame_vbar(tree,state,kind, t.g_x, 0, t.g_w+2, t.g_h+2);
    }

    /* do horizontal bar area */
    if (havehbar)
    {
        t.g_y += t.g_h;
        frame_hbar(tree,state,kind, 0, t.g_y, t.g_w+2, t.g_h+2);
    }

    /* do sizer area */
    if (corner_x >= 0)
    {
        frame_colour(tree, state, W_SIZER);
        frame_adjust(tree, W_DATA, W_SIZER, corner_x, corner_y, state->wbox, state->hbox);
        /* we only display the sizer indicator if we're really a SIZER */
        tree[W_SIZER].ob_spec.index &= 0x00ffffffL;   /* remove gadget char */
        if (kind & SIZER)
            tree[W_SIZER].ob_spec.index |= 0x06000000L;
    }
}