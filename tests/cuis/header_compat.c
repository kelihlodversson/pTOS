/* Transitional native bindings can coexist with standard GEM selectors. */
#if LEGACY_HEADER_FIRST == 2
#include "aes_client.h"
#elif LEGACY_HEADER_FIRST
#include "aes_client.h"
#include "gem.h"
#else
#include "gem.h"
#include "aes_client.h"
#endif
int main(void)
{
#if LEGACY_HEADER_FIRST == 2
    AES_MESSAGE message = { 0 };
    return sizeof(message) != sizeof(AES_CLIENT_MESSAGE);
#else
    AES_CLIENT_MESSAGE message = { 0 };
    if (SWM_LAUNCHACC != 3 || SWM_BROADCAST != 7 || SWM_ENVIRON != 8 ||
        SWM_THRCREATE != 20 || SWM_THREXIT != 21 || SWM_THRKILL != 22 ||
        SW_PSETLIMIT != 0x0100 || SW_PRENICE != 0x0200 || SW_DEFDIR != 0x0400 ||
        SW_ENVIRON != 0x0800 || SW_UID != 0x1000 || SW_GID != 0x2000 ||
        XSHW_RUNANY != 224 || XSHW_RUNAPP != 225 || XSHW_RUNACC != 227 || XSHD_FLAGS != 0x8000 ||
        ENVIRON_SIZE != 0 || ENVIRON_CHANGE != 1 || ENVIRON_COPY != 2 ||
        NM_INHIBIT_HIDE != 2 || AP_AESTERM != 52) return 1;
    return AES_MESSAGE != 12 || sizeof(message) != 16;
#endif
}
