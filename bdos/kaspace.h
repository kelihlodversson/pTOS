/* Independent ownership of an architecture address-space backend. */
#ifndef KASPACE_H
#define KASPACE_H

#include "portab.h"
struct aspace;
typedef struct kaspace KASPACE;

/* backend must be exclusively owned by the caller and not previously adopted.
 * On success transfers ownership; on failure ownership stays with the caller.
 * References may be manipulated only in serialized process context. */
KASPACE *kaspace_adopt(struct aspace *backend);
/* Return another owning reference, or NULL on reference-count exhaustion. */
KASPACE *kaspace_share(KASPACE *space);
/* Borrowed pointer: valid only while the caller holds an owning reference. */
struct aspace *kaspace_backend(const KASPACE *space);
/* Drop one owning reference; the final drop destroys the backend. */
void kaspace_put(KASPACE *space);
/* Independent cloning requires an architecture backend; none exists yet. */
LONG kaspace_clone(const KASPACE *source, KASPACE **result);

#if CONF_WITH_X86_64_MEMTEST
/* Fail the next adoption, leaving backend ownership with its caller. */
void kaspace_test_fail_next(void);
#endif

#endif
