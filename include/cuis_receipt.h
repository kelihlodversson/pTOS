/* Fixed-width endpoint stream publication receipt. */
#ifndef CUIS_RECEIPT_H
#define CUIS_RECEIPT_H
#include "portab.h"
#define CUIS_RECEIPT_SLOTS 16U
#define CUIS_RECEIPT_QUERY 0UL
#define CUIS_RECEIPT_PREPARE 1UL
#define CUIS_RECEIPT_COMMIT 2UL
#define CUIS_RECEIPT_ACK 3UL
#define CUIS_RECEIPT_RECLAIM 4UL
#define CUIS_RECEIPT_UNBOUND 0UL
#define CUIS_RECEIPT_BOUND 1UL
#define CUIS_RECEIPT_PREPARED 2UL
#define CUIS_RECEIPT_COMMITTED 3UL
#define CUIS_RECEIPT_ACKNOWLEDGED 4UL
#define CUIS_RECEIPT_RECLAIMED 5UL
/* QUERY retains the terminal publication outcome in its bounded floor. */
#define CUIS_RECEIPT_COMMITTED_OUTCOME 0x80000000UL
typedef struct {
    ULONG operation, source, destination, slot;
    ULONG source_low, source_high, destination_low, destination_high;
    ULONG attempt_low, attempt_high, key_low, key_high;
    ULONG packet, bytes, state, tag;
} __attribute__((aligned(4))) CUIS_RECEIPT_RECORD;
typedef char CUIS_RECEIPT_LAYOUT[
    sizeof(CUIS_RECEIPT_RECORD)==64 && __alignof__(CUIS_RECEIPT_RECORD)==4 &&
    offsetof(CUIS_RECEIPT_RECORD,source)==4 && offsetof(CUIS_RECEIPT_RECORD,slot)==12 &&
    offsetof(CUIS_RECEIPT_RECORD,attempt_low)==32 && offsetof(CUIS_RECEIPT_RECORD,packet)==48 &&
    offsetof(CUIS_RECEIPT_RECORD,tag)==60 ? 1:-1];
#define CUIS_RECEIPT_OFFSET(field,byte) \
    typedef char cuis_receipt_offset_##field[offsetof(CUIS_RECEIPT_RECORD,field)==byte ? 1:-1]
CUIS_RECEIPT_OFFSET(operation,0);
CUIS_RECEIPT_OFFSET(source,4);
CUIS_RECEIPT_OFFSET(destination,8);
CUIS_RECEIPT_OFFSET(slot,12);
CUIS_RECEIPT_OFFSET(source_low,16);
CUIS_RECEIPT_OFFSET(source_high,20);
CUIS_RECEIPT_OFFSET(destination_low,24);
CUIS_RECEIPT_OFFSET(destination_high,28);
CUIS_RECEIPT_OFFSET(attempt_low,32);
CUIS_RECEIPT_OFFSET(attempt_high,36);
CUIS_RECEIPT_OFFSET(key_low,40);
CUIS_RECEIPT_OFFSET(key_high,44);
CUIS_RECEIPT_OFFSET(packet,48);
CUIS_RECEIPT_OFFSET(bytes,52);
CUIS_RECEIPT_OFFSET(state,56);
CUIS_RECEIPT_OFFSET(tag,60);
#undef CUIS_RECEIPT_OFFSET
#endif
