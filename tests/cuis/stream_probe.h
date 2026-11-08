/* Native test protocol over the production opaque byte stream. Endpoint is
 * application payload for reply routing, not authenticated sender provenance.
 * Return the stream's byte counts and SESSION_WAIT masks unchanged. */
#ifndef CUIS_STREAM_PROBE_H
#define CUIS_STREAM_PROBE_H
#include "native_probe.h"
typedef struct {
    LONG endpoint;
    ULONG word[4];
} PROBE_PACKET;
static inline LONG probe_packet_write(LONG destination, LONG endpoint, ULONG a, ULONG b, ULONG c, ULONG d)
{
    PROBE_PACKET packet;
    packet.endpoint = endpoint;
    packet.word[0] = a; packet.word[1] = b; packet.word[2] = c; packet.word[3] = d;
    return probe_call6(CUIS_STREAM_WRITE,(QUAD)destination,(uintptr_t)&packet,sizeof(packet),0,0,0);
}
static inline LONG probe_packet_read(LONG session, PROBE_PACKET *packet)
{
    return probe_call6(CUIS_STREAM_READ,(QUAD)session,(uintptr_t)packet,sizeof(*packet),0,0,0);
}
static inline LONG probe_packet_wait(LONG session, LONG milliseconds)
{
    return probe_call6(CUIS_SESSION_WAIT,(QUAD)session,sizeof(PROBE_PACKET),
                      CUIS_READY_STREAM | (milliseconds < 0 ? 0 : CUIS_READY_TIMER),
                      milliseconds < 0 ? 0 : (ULONG)milliseconds,0,0);
}
#endif
