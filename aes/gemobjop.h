/*
 * gemobjop.h - header for EmuTOS AES miscellaneous object-related functions
 *
 * Copyright (C) 2002-2022 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GEMOBJOP_H
#define GEMOBJOP_H

/* Native AES retains WORD traversal; libaes validates wide positions before
 * passing them to the original painter. */
#ifndef OB_WALK_COORD
#define OB_WALK_COORD WORD
#endif
typedef void (*EVERYOBJ_CALLBACK)(OBJECT *tree, WORD obj, OB_WALK_COORD sx, OB_WALK_COORD sy);

BYTE ob_sst(OBJECT *tree, WORD obj, OBSPEC *pspec, WORD *pstate, WORD *ptype,
            WORD *pflags, GRECT *pt, WORD *pth);
void everyobj(OBJECT *tree, WORD this, WORD last, EVERYOBJ_CALLBACK routine,
              OB_WALK_COORD startx, OB_WALK_COORD starty, WORD maxdep);
WORD get_par(OBJECT *tree, WORD obj);

#endif
