/* Translate retained native completions into ordinary GEM messages. */
#include "gem.h"
#include "application.h"
#include "transport.h"
#include "gemerror.h"
LONG aes_child_messages(LONG session)
{
    ULONG i;
    for (i = 0; i < CUIS_CHILDREN_PER_SESSION; i++) {
        LONG status = cuis_call6(CUIS_CHILD_RESULT,session,0,0,0,0,0), result;
        short message[8] = { CH_EXIT,0,0,0,0,0,0,0 };
        if (status <= 0) return status;
        message[3] = (short)((ULONG)status >> 16);
        message[4] = (short)status;
        result = cuis_call6(CUIS_CHILD_PUBLISH,session,(ULONG)status,(uintptr_t)message,sizeof(message),0,0);
        if (result == EAGAIN) return E_OK;
        if (result != E_OK) return result;
    }
    return E_OK;
}
