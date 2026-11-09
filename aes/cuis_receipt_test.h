/* Narrow internal C entry points for the default-off #907 receipt prototype.
 * No trap selector, STREAM extension or production userspace ABI is exposed. */
#ifndef CUIS_RECEIPT_TEST_H
#define CUIS_RECEIPT_TEST_H
#include "portab.h"
#if CONF_WITH_CUIS_RECEIPT_TEST
#define CUIS_RECEIPT_BOUND 1UL
#define CUIS_RECEIPT_PREPARED 2UL
#define CUIS_RECEIPT_COMMITTED 3UL
#define CUIS_RECEIPT_ACKNOWLEDGED 4UL
#define CUIS_RECEIPT_RECLAIMED 5UL
typedef struct {
    ULONG operation, source, destination, slot;
    ULONG source_low, source_high, destination_low, destination_high;
    ULONG attempt_low, attempt_high, key_low, key_high;
    ULONG packet, bytes, state, tag;
} __attribute__((aligned(4))) CUIS_RECEIPT_TEST_RECORD;
typedef char CUIS_RECEIPT_TEST_LAYOUT[
    sizeof(CUIS_RECEIPT_TEST_RECORD)==64 && __alignof__(CUIS_RECEIPT_TEST_RECORD)==4 &&
    offsetof(CUIS_RECEIPT_TEST_RECORD,source)==4 && offsetof(CUIS_RECEIPT_TEST_RECORD,slot)==12 &&
    offsetof(CUIS_RECEIPT_TEST_RECORD,attempt_low)==32 && offsetof(CUIS_RECEIPT_TEST_RECORD,packet)==48 &&
    offsetof(CUIS_RECEIPT_TEST_RECORD,tag)==60 ? 1:-1];
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
#define CUIS_RECEIPT_TEST_OFFSET(field,byte) \
    typedef char cuis_receipt_test_offset_##field[offsetof(CUIS_RECEIPT_TEST_RECORD,field)==byte ? 1:-1]
CUIS_RECEIPT_TEST_OFFSET(operation,0);
CUIS_RECEIPT_TEST_OFFSET(source,4);
CUIS_RECEIPT_TEST_OFFSET(destination,8);
CUIS_RECEIPT_TEST_OFFSET(slot,12);
CUIS_RECEIPT_TEST_OFFSET(source_low,16);
CUIS_RECEIPT_TEST_OFFSET(source_high,20);
CUIS_RECEIPT_TEST_OFFSET(destination_low,24);
CUIS_RECEIPT_TEST_OFFSET(destination_high,28);
CUIS_RECEIPT_TEST_OFFSET(attempt_low,32);
CUIS_RECEIPT_TEST_OFFSET(attempt_high,36);
CUIS_RECEIPT_TEST_OFFSET(key_low,40);
CUIS_RECEIPT_TEST_OFFSET(key_high,44);
CUIS_RECEIPT_TEST_OFFSET(packet,48);
CUIS_RECEIPT_TEST_OFFSET(bytes,52);
CUIS_RECEIPT_TEST_OFFSET(state,56);
CUIS_RECEIPT_TEST_OFFSET(tag,60);
#undef CUIS_RECEIPT_TEST_OFFSET
#endif
#endif
