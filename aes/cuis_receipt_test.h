/* Narrow internal C entry points for the default-off #907 receipt prototype.
 * No trap selector, STREAM extension or production userspace ABI is exposed. */
#ifndef CUIS_RECEIPT_TEST_H
#define CUIS_RECEIPT_TEST_H
#include "portab.h"
#if CONF_WITH_CUIS_RECEIPT_TEST
#include "../include/cuis_receipt.h"
typedef CUIS_RECEIPT_RECORD CUIS_RECEIPT_TEST_RECORD;
/* Trusted transitional ASYNC launch fixture only. Installs the recoverable
 * entitlement before COMMIT; no GUI callback from the generic launcher. */
LONG cuis_receipt_test_launch(ULONG source,ULONG destination,ULONG slot,
                             char *path,char *tail,char *environment,
                             CUIS_RECEIPT_TEST_RECORD *out);
LONG cuis_receipt_test_query(CUIS_RECEIPT_TEST_RECORD *record);
LONG cuis_receipt_test_prepare(const CUIS_RECEIPT_TEST_RECORD *record);
LONG cuis_receipt_test_commit(const CUIS_RECEIPT_TEST_RECORD *record);
LONG cuis_receipt_test_ack(const CUIS_RECEIPT_TEST_RECORD *record);
LONG cuis_receipt_test_reclaim(const CUIS_RECEIPT_TEST_RECORD *record);
#endif
#endif
