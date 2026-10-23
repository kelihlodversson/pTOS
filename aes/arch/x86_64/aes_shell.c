/* Only bounded copies reach the native shell. No retained user pointers.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "aes_shell.h"
#include "aes_accessory.h"
#include "aes_service.h"
#include "struct.h"
#include "aesvars.h"
#include "gemlib.h"
#include "gemshlib.h"
#include "gemfmlib.h"
#include "gemevlib.h"
#include "gemmnlib.h"
#include "gemwmlib.h"
#include "geminit.h"
#include "gemerror.h"
#include "string.h"
#include "../../../bios/ikbd.h"
#include "../../../bdos/kproc.h"
#include "../../../bdos/bdosstub.h"

static LONG path(ULONG address, char *buffer)
{
    ULONG i;

    for (i = 0; i < AES_SHELL_PATH_MAX; i++) {
        if (!kproc_copy_from_user(buffer + i, (UQUAD)address + i, 1))
            return EIMBA;
        if (!buffer[i])
            return E_OK;
    }
    return ERANGE;
}

LONG x86_64_aes_shell(UQUAD address)
{
    AES_SHELL_REQUEST r, unused, zero = { 0 };
    char first[AES_SHELL_PATH_MAX], second[AES_SHELL_PATH_MAX];
    UBYTE buffer[AES_SHELL_BUFFER_MAX];
    LONG rc;
    GRECT empty = { 0 };

    if ((address & 3) || !kproc_copy_from_user(&r, address, sizeof(r)))
        return EIMBA;
    if (!x86_64_aes_owned(run))
        return EACCDN;
    unused = r;
    unused.operation = 0;
    switch (r.operation) {
    case AES_SHELL_WRITE:
        unused.value = unused.extra = 0;
        unused.data = unused.second = unused.bytes = 0;
        break;
    case AES_SHELL_RESET:
        break;
    case AES_SHELL_WDEF:
        unused.data = unused.second = 0;
        break;
    case AES_SHELL_GET: case AES_SHELL_PUT:
        unused.data = unused.bytes = 0;
        break;
    case AES_SHELL_KEY:
        unused.value = 0;
        break;
    case AES_SHELL_DCLICK:
        unused.value = unused.extra = 0;
        break;
    case AES_SHELL_DAMAGE:
        memset(&unused.rectangle, 0, sizeof(unused.rectangle));
        break;
    default:
        return EINVFN;
    }
    if (memcmp(&unused, &zero, sizeof(zero)))
        return EINVAL;
    if (r.operation == AES_SHELL_KEY)
        return r.value < 0 || r.value > 127 ? EINVAL : keyboard_shifted(r.value);
    if (r.operation == AES_SHELL_DCLICK) {
        if (r.value < 0 || r.value > 4 || r.extra < 0 || r.extra > 1)
            return EINVAL;
        return ev_dclick(r.value, r.extra);
    }
    if (r.operation == AES_SHELL_DAMAGE) {
        if (r.rectangle.g_w <= 0 || r.rectangle.g_h <= 0 ||
            r.rectangle.g_x < -10000 || r.rectangle.g_y < -10000 ||
            r.rectangle.g_x > 10000 || r.rectangle.g_y > 10000 ||
            r.rectangle.g_w > 10000 || r.rectangle.g_h > 10000)
            return EINVAL;
        if ((wind_spb.sy_tas && wind_spb.sy_owner != rlr) ||
            !aes_service_window_unlocked(run))
            return EAGAIN;
        wm_update(BEG_UPDATE);
        rc = fm_dial(FMD_FINISH, &empty, &r.rectangle);
        wm_update(END_UPDATE);
        return rc;
    }
    /* The foreground application owns the shell continuation. Accessories
     * cannot overwrite its pending program or desktop settings. */
    if (rlr->p_pid != 0)
        return EACCDN;
    switch (r.operation) {
    case AES_SHELL_RESET:
        if (!aes_service_window_unlocked(run))
            return EAGAIN;
        mn_cleanup();
        wait_for_accs(AP_ACCLOSE);
        wm_new();
        x86_64_aes_shell_closed(run);
        return E_OK;
    case AES_SHELL_GET: case AES_SHELL_PUT:
        if (r.bytes > sizeof(buffer))
            return ERANGE;
        if (r.operation == AES_SHELL_GET) {
            sh_get(buffer, r.bytes);
            return kproc_copy_to_user(r.data, buffer, r.bytes) ? E_OK : EIMBA;
        }
        if (!kproc_copy_from_user(buffer, r.data, r.bytes))
            return EIMBA;
        sh_put(buffer, r.bytes);
        return E_OK;
    case AES_SHELL_WDEF:
        rc = path(r.data, first);
        if (!rc)
            rc = path(r.second, second);
        if (rc)
            return rc;
        if (strlen(first) >= LEN_ZFNAME || strlen(second) >= LEN_ZPATH)
            return ERANGE;
        sh_wdef(first, second);
        return E_OK;
    case AES_SHELL_WRITE:
        /* bytes carries the existing isover scalar, never a buffer size. */
        if (r.bytes > 2 || (r.value != SHW_NOEXEC && r.value != SHW_EXEC &&
            r.value != SHW_SHUTDOWN && r.value != SHW_RESCHNG))
            return EINVAL;
        if (r.value == SHW_EXEC) {
            char *name;

            if (r.extra < 0 || r.extra > 1 || (rc = path(r.data, first)) != E_OK)
                return r.extra < 0 || r.extra > 1 ? EINVAL : rc;
            /* Native shell search concatenates this basename with its
             * application directory. Enforce the TOS component limits,
             * not only the size of the copied complete command. */
            name = sh_name(first);
            if (!*name)
                return EINVAL;
            if (strlen(name) >= LEN_ZFNAME || name - first >= LEN_ZPATH)
                return ERANGE;
            if (!kproc_copy_from_user(second, r.second, AES_SHELL_TAIL_BYTES))
                return EIMBA;
        } else {
            if (r.data || r.second)
                return EINVAL;
            first[0] = second[0] = 0;
        }
        return sh_write(r.value, r.extra, r.bytes, first, second);
    }
    return EINVFN;
}
