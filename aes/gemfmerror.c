/* Shared GEM error alerts, from gemfmlib.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "gemfmlib.h"
#include "gemfmalt.h"
#include "gemrslib.h"
#include "gem_rsc.h"
#include "string.h"

static char alert_str[256];

/*
 * issue form_alert(), with optional drive letter
 */
WORD fm_show(WORD string, WORD *pwd, WORD level)
{
    char *ad_alert;

    ad_alert = rs_str(string);
    if (pwd)
    {
        sprintf(alert_str, ad_alert, *pwd);
        ad_alert = alert_str;
    }

    return fm_alert(level, ad_alert);
}


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
