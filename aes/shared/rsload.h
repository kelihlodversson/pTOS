/*
 *  rsload.h - private interface between the shared RSC library code and
 *  the two alternative resource loaders (legacy in-place and portable
 *  canonical).  Exactly one of the loaders is linked, selected by build.mk.
 */

#ifndef RSLOAD_H
#define RSLOAD_H

#include "config.h"
#include "portab.h"
#include "rsdefs.h"
#include "gemrslib.h"

#define R_TREE      0
#define R_OBJECT    1
#define R_TEDINFO   2
#define R_ICONBLK   3
#define R_BITBLK    4
#define R_STRING    5               /* gets pointer to free strings */
#define R_IMAGEDATA 6               /* gets pointer to free images  */
#define R_OBSPEC    7
#define R_TEPTEXT   8               /* sub ptrs in TEDINFO  */
#define R_TEPTMPLT  9
#define R_TEPVALID  10
#define R_IBPMASK   11              /* sub ptrs in ICONBLK  */
#define R_IBPDATA   12
#define R_IBPTEXT   13
#define R_BIPDATA   14              /* sub ptrs in BITBLK   */
#define R_FRSTR     15              /* gets addr of ptr to free strings     */
#define R_FRIMG     16              /* gets addr of ptr to free images      */

extern RSHDR   *rs_hdr;
extern AESGLOBAL *rs_global;

WORD rs_readit(AESGLOBAL *pglobal, UWORD fd);
void fix_objects(void);

void *get_addr(UWORD rstype, UWORD rsindex);
void *get_sub(UWORD rsindex, UWORD offset, UWORD rsize);
CICONBLK **get_ciconblkptr(RSHDR *hdr);
void transform_all_cicons(LONG num_cicons, CICONBLK **ciconblkptr);

#endif
