/* REVIEW-ONLY proposal. Not a public header, selector or production handler. */
#ifndef TEST_PUBLICATION_RECEIPT_WIRE_H
#define TEST_PUBLICATION_RECEIPT_WIRE_H
#include "portab.h"
typedef struct {
    ULONG operation, source, destination, slot;
    ULONG source_generation_low, source_generation_high;
    ULONG destination_generation_low, destination_generation_high;
    ULONG attempt_low, attempt_high;
    ULONG key_low, key_high;
    ULONG packet_address, packet_bytes, state, adapter_tag;
} __attribute__((aligned(4))) TEST_PUBLICATION_RECEIPT;
#define RECEIPT_OFFSET(field, byte) \
    typedef char receipt_offset_##field[offsetof(TEST_PUBLICATION_RECEIPT,field)==byte ? 1 : -1]
RECEIPT_OFFSET(operation,0);
RECEIPT_OFFSET(source,4);
RECEIPT_OFFSET(destination,8);
RECEIPT_OFFSET(slot,12);
RECEIPT_OFFSET(source_generation_low,16);
RECEIPT_OFFSET(source_generation_high,20);
RECEIPT_OFFSET(destination_generation_low,24);
RECEIPT_OFFSET(destination_generation_high,28);
RECEIPT_OFFSET(attempt_low,32);
RECEIPT_OFFSET(attempt_high,36);
RECEIPT_OFFSET(key_low,40);
RECEIPT_OFFSET(key_high,44);
RECEIPT_OFFSET(packet_address,48);
RECEIPT_OFFSET(packet_bytes,52);
RECEIPT_OFFSET(state,56);
RECEIPT_OFFSET(adapter_tag,60);
#undef RECEIPT_OFFSET
typedef char receipt_size[sizeof(TEST_PUBLICATION_RECEIPT)==64 ? 1 : -1];
typedef char receipt_alignment[__alignof__(TEST_PUBLICATION_RECEIPT)==4 ? 1 : -1];
typedef char receipt_scalar_width[sizeof(ULONG)==4 ? 1 : -1];
typedef char receipt_address_width[sizeof(((TEST_PUBLICATION_RECEIPT *)0)->packet_address)==4 ? 1 : -1];
#endif
