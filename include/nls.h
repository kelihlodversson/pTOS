/*
 * nls.h - Definitions for Native Language Support
 *
 * Copyright (C) 2001 The Emutos Development Team
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef NLS_H
#define NLS_H

#include "config.h"
#include "i18nconf.h"

/* the gettext-like macros */

#if CONF_WITH_NLS

/*
 * A message id is a small number with these bits set; a string that is not
 * marked (N_()) is a pointer, which never has them all set.  With a
 * higher-half kernel, a string of the kernel image is at 0x80000000 and up
 * (the physical-memory window, below the devices at 0xc0000000), so the top
 * bit alone does not tell a pointer from an id.
 */
#if CONF_WITH_HIGHER_HALF
# define NLS_MSGID_FLAG 0xc0000000UL
#else
# define NLS_MSGID_FLAG 0x80000000UL
#endif

# define _(a) etos_gettext((unsigned long)(a))
# define N_(a) ((char *)((unsigned long)(a) | NLS_MSGID_FLAG))
# define gettext(a) etos_gettext((unsigned long)(a))
const char *etos_gettext(unsigned long msgid);

/* initialisation */

void nls_init(void);

/* functions to query the lang database and to set the lang */

void nls_set_lang(const char *);

#else

/* Disable NLS / gettext completely */

# define _(a) (a)
# define N_(a) a
# define gettext(a) (a)

#endif

#endif /* NLS_H */
