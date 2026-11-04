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
