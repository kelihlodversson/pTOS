/* Original GEM form_error mapping and return conventions.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "gemfmlib.h"
#include "gem_rsc.h"
#ifdef AES_ERROR_BINDINGS
#include AES_ERROR_BINDINGS
#endif

WORD fm_error(WORD n)             /* n = dos error number */
{
    WORD    ret, string;

    if (n > 63)
        return FALSE;

    switch (n)
    {
    case 2:
    case 18:
    case 3:
        string = AL18ERR;
        break;
    case 4:
        string = AL04ERR;
        break;
    case 5:
        string = AL05ERR;
        break;
    case 15:
        string = AL15ERR;
        break;
    case 8:
    case 10:
    case 11:
        string = AL08ERR;
        break;
    default:
        string = ALXXERR;
        break;
    }

    ret = fm_show(string, ((string == ALXXERR) ? &n : 0), 1);

    return (ret != 1);
}
