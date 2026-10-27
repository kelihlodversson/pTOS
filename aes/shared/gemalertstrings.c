/* Original GEM system alert strings, shared with caller-local forms.
 * Copyright 2013-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#ifdef AES_RESOURCE_STRINGS_LOCAL
#define N_(text) text
#else
#include "nls.h"
#endif

const char * const rs_fstr[] = {
    N_("[1][The disk in drive %c: is|"                  /* AL00CRT */
       "physically write-protected.][Cancel|Retry]"),
    N_("[2][Drive %c: is not responding.|"              /* AL01CRT */
       "Please check the disk drive,|"
       "or insert a disk.][Cancel|Retry]"),
    N_("[1][Data on the disk in drive %c:|"             /* AL02CRT */
       "may be damaged.][Cancel|Retry]"),
    N_("[2][This application cannot read|"              /* AL03CRT */
       "data on the disk in drive %c:.][Cancel|Retry]"),
    N_("[1][Your output device is not|"                 /* AL04CRT */
       "receiving data.][Cancel|Retry]"),
    N_("[3][An error has occurred in GEM.|"             /* AL05CRT */
       "Please contact the EmuTOS|"
       "Development Team.][Cancel]"),
    N_("[2][This application cannot|"                   /* AL18ERR */
       "find the folder or file|"
       "you just tried to access.][  OK  ]"),
    N_("[1][This application does not|"                 /* AL04ERR */
       "have room to open another|"
       "document.  To make room,|"
       "close any document that|"
       "you do not need.][  OK  ]"),
    N_("[1][An item with this name|"                    /* AL05ERR */
       "already exists in the|"
       "directory, or this item|"
       "is set to Read Only status.][  OK  ]"),
    N_("[1][The drive you specified|"                   /* AL15ERR */
       "does not exist.][Cancel]"),
    N_("[1][There is not enough memory|"                /* AL08ERR */
       "in your computer for the|"
       "application you just tried|"
       "to run.][  OK  ]"),
    N_("[3][TOS error #%u.][Cancel]"),                  /* ALXXERR */
    N_("[3][Unsupported AES function #%d.][Cancel]"),   /* ALNOFUNC */
    N_("[1][Not enough memory for|"                     /* ALFSMEM */
       "this directory][  OK  ]"),
    N_("ITEM SELECTOR"),                                /* ITEMSLCT */
};


