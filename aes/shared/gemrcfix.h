/* Native caller-owned resource relocation, separate from disk decoding. */
#ifndef GEMRCFIX_H
#define GEMRCFIX_H
#include "portab.h"
#include "rsdefs.h"
/* source stays unchanged. staged has the same size/layout, and receives
 * relocated pointers into source. Caller commits only after all checks pass. */
BOOL rs_rcfix_native(const RSHDR *source, RSHDR *staged);
#endif
