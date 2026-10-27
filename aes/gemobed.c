/*      GEMOBED.C       05/29/84 - 06/20/85             Gregg Morris    */
/*      merge High C vers. w. 2.2               8/21/87         mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2020 The EmuTOS development team
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
#include "struct.h"
#include "obdefs.h"
#include "aesext.h"
#include "intmath.h"
#include "gemlib.h"

#include "gemoblib.h"
#include "gemgraf.h"
#include "geminit.h"
#include "gemobjop.h"
#include "gemobed.h"
#include "gemlayout.h"

#include "string.h"
#include "scancode.h"


void ob_center(OBJECT *tree, GRECT *pt)
{
    ob_center_metrics(tree,pt,gl_width,gl_height,gl_hbox);
}
