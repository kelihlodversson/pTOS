/* Public globals can be included standalone and shared with classic resources. */
#include "aesglobal.h"
#if VDI_HEADER_FIRST
#include "vdi_user.h"
#include "gem.h"
#else
#include "gem.h"
#include "vdi_user.h"
#endif
#include "gemrslib.h"
typedef char init_takes_typed_global[
    __builtin_types_compatible_p(__typeof__(&mt_appl_init), short (*)(AESGLOBAL *)) ? 1 : -1];
typedef char default_is_typed_global[
    __builtin_types_compatible_p(__typeof__(aes_global), AESGLOBAL) ? 1 : -1];
int main(void)
{
    if (SWM_LAUNCHACC != 3 || SWM_BROADCAST != 7 || SWM_ENVIRON != 8 ||
        SWM_THRCREATE != 20 || SWM_THREXIT != 21 || SWM_THRKILL != 22 ||
        SW_PSETLIMIT != 0x0100 || SW_PRENICE != 0x0200 || SW_DEFDIR != 0x0400 ||
        SW_ENVIRON != 0x0800 || SW_UID != 0x1000 || SW_GID != 0x2000 ||
        XSHW_RUNANY != 224 || XSHW_RUNAPP != 225 || XSHW_RUNACC != 227 || XSHD_FLAGS != 0x8000 ||
        ENVIRON_SIZE != 0 || ENVIRON_CHANGE != 1 || ENVIRON_COPY != 2 ||
        NM_INHIBIT_HIDE != 2 || AP_AESTERM != 52) return 1;
    return AES_MESSAGE != 12;
}
