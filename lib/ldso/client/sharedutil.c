/* Out-of-line shared versions of the existing pTOS string helpers.
 * Keep the kernel's inline policy local to its own translation units. */
#include "emutos.h"
#undef USE_STATIC_INLINES
#define USE_STATIC_INLINES 0
#include "../../../util/string.c"
