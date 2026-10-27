/*
 * miscutil.c - miscellaneous utility functions
 *
 * Copyright (C) 2022 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "emutos.h"
#include "string.h"
#include "miscutil.h"
#include "obdefs.h"
#include "nls.h"
#include "aesext.h"

/*
 *  builds root path for specified drive
 */
void build_root_path(char *path, char drive)
{
    char *p = path;

    *p++ = drive;
    *p++ = DRIVESEP;
    *p++ = PATHSEP;
    *p = '\0';
}


/*
 * returns the drive number corresponding to the drive prefix (X:) in
 * the passed string
 *
 * returns  0->25 for drives A thru Z, or
 *          -1 if a valid drive prefix does not exist
 */
WORD extract_drive_number(const char *path)
{
    char c;

    if (path[0] && (path[1] == DRIVESEP))
    {
        c = toupper(path[0]);
        if ((c >= 'A') && (c <= 'Z'))
            return c - 'A';
    }

    return -1;
}


/*
 *  Copies "*.*" to the specified position in a path string
 */
void set_all_files(char *target)
{
    strcpy(target,"*.*");
}


#if CONF_WITH_AES
/*
 *  The xlate_obj_array() function below is used by
 *  the generated GEM rsc code in aes/shared/gem_rsc.c, and by the desktop
 */

/* Translates the strings in an OBJECT array */
void xlate_obj_array(OBJECT *obj_array, int nobj)
{
    OBJECT *obj;
    char **str;

    for (obj = obj_array; --nobj >= 0; obj++) {
        switch(obj->ob_type)
        {
#if 0
        /*
         * at the moment, there are no G_TEXT or G_BOXTEXT items in the
         * EmuTOS resources.  note that, if they are added, erd will have
         * to be updated too.
         */
        case G_TEXT:
        case G_BOXTEXT:
            str = &obj->ob_spec.tedinfo->te_ptext;
            *str = (char *)gettext(*str);
            break;
#endif
        case G_FTEXT:
        case G_FBOXTEXT:
            str = &obj->ob_spec.tedinfo->te_ptmplt;
            *str = (char *)gettext(*str);
            break;
        case G_STRING:
        case G_BUTTON:
        case G_TITLE:
            obj->ob_spec.free_string = (char *)gettext(obj->ob_spec.free_string);
            break;
        default:
            break;
        }
    }
}
#endif /* CONF_WITH_AES */
