/* A remote fid can be reused only after a complete, matching Tclunk reply.
 * Pure validation shared with the production-code host fault fixture. */
#ifndef VIRTIO_9P_CLUNK_H
#define VIRTIO_9P_CLUNK_H
#include "portab.h"
static BOOL v9p_clunk_terminal(const UBYTE *reply, LONG bytes, UWORD tag)
{
    ULONG size;
    if (bytes<7) return FALSE;
    size=(ULONG)reply[0] | ((ULONG)reply[1]<<8) |
         ((ULONG)reply[2]<<16) | ((ULONG)reply[3]<<24);
    if (size!=(ULONG)bytes || reply[5]!=(UBYTE)tag || reply[6]!=(UBYTE)(tag>>8))
        return FALSE;
    return (reply[4]==121 && bytes==7) || (reply[4]==7 && bytes==11);
}
#endif
