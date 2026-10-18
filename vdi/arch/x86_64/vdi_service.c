/*
 * vdi_service.c - narrow, checked supervisor operations for x32 VDI
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "emutos.h"
#include "vdi_service.h"
#include "vdi_defs.h"
#include "lineavars.h"
#include "gemerror.h"
#include "string.h"
#include "biosbind.h"
#include "procmem.h"
#include "../../../bdos/kproc.h"

extern PD *run;

static PD *owners[NUM_VDI_HANDLES + 1];
static BOOL physical_owned, physical_active;

static void invoke(VDICONTROL *control, WORD *intin, WORD *ptsin,
                    WORD *intout, WORD *ptsout)
{
    VDIPB saved = linea_vars.local_pb;
    VDIPB block;

    block.contrl = control;
    block.intin = intin;
    block.ptsin = ptsin;
    block.intout = intout;
    block.ptsout = ptsout;
    GSX_ENTRY(0x73, &block);
    /* No user pointers or pointers to this stack may outlive the call. */
    linea_vars.local_pb = saved;
}

static void physical_open(void)
{
    VDICONTROL control = { 0 };
    WORD input[11] = { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 2 };
    WORD integers[45], points[12];

    if (physical_active || (!physical_owned && get_vwk_by_handle(1)))
        return;
    control.code = 1;
    control.nintin = 11;
    invoke(&control, input, points, integers, points);
    physical_owned = physical_active = TRUE;
}

static void physical_close_if_idle(void)
{
    VDICONTROL control = { 0 };
    WORD input = 0, integers[45], points[12];
    int i;

    if (!physical_owned || !physical_active)
        return;
    for (i = 2; i <= NUM_VDI_HANDLES; i++)
        if (owners[i])
            return;
    control.code = 2;
    control.handle = 1;
    invoke(&control, &input, points, integers, points);
    physical_active = FALSE;
}

static BOOL owned(UQUAD handle)
{
    return handle >= 2 && handle <= NUM_VDI_HANDLES &&
           owners[handle] == run && get_vwk_by_handle((WORD)handle) != NULL;
}

static void close_handle(WORD handle)
{
    VDICONTROL control = { 0 };
    WORD input = 0, integers[45], points[12];

    control.code = 101;
    control.handle = handle;
    invoke(&control, &input, points, integers, points);
    owners[handle] = NULL;
}

void vdi_service_process_exit(PD *pd)
{
    WORD handle;

    for (handle = 2; handle <= NUM_VDI_HANDLES; handle++)
        if (owners[handle] == pd)
            close_handle(handle);
    physical_close_if_idle();
}

static LONG open_workstation(UQUAD input, UQUAD output)
{
    VDICONTROL control = { 0 };
    VDI_SERVICE_WORKSTATION result;
    WORD work_in[11];

    /* Back the destination before opening anything: an OOM while backing
     * lazy user memory ends the process and must not strand a new Vwk. */
    if (!kproc_copy_from_user(work_in, input, sizeof(work_in)) ||
        !kproc_validate_user_write(output, sizeof(result)))
        return EIMBA;
    if (work_in[10] != 2)
        return EINVAL;                  /* native raster-coordinate convention */
    physical_open();
    control.code = 100;
    control.nintin = 11;
    invoke(&control, work_in, result.ptsout, result.intout, result.ptsout);
    if (!control.handle) {
        physical_close_if_idle();
        return ENSMEM;
    }
    result.handle = control.handle;
    owners[result.handle] = run;
    if (!kproc_copy_to_user(output, &result, sizeof(result))) {
        close_handle(result.handle);
        physical_close_if_idle();
        return EIMBA;
    }
    return E_OK;
}

static LONG coordinate_call(ULONG fn, UQUAD handle, UQUAD value, UQUAD address)
{
    VDICONTROL control = { 0 };
    WORD points[VDI_SERVICE_MAX_POINTS * 2];
    WORD input, output[45], outpoints[12];
    ULONG words, i;

    control.handle = (WORD)handle;
    if (fn == VDI_SERVICE_COLOR) {
        if (value > 32767)
            return EINVAL;
        control.code = 17;
        control.nintin = 1;
        input = (WORD)value;
        words = 0;
    } else if (fn == VDI_SERVICE_CLIP) {
        if (value > 1)
            return EINVAL;
        control.code = 129;
        control.nintin = 1;
        control.nptsin = 2;
        input = (WORD)value;
        words = 4;
    } else {
        if (value < 2 || value > VDI_SERVICE_MAX_POINTS)
            return EINVAL;
        control.code = 6;
        control.nptsin = (WORD)value;
        input = 0;
        words = (ULONG)value * 2;
    }
    if (words && !kproc_copy_from_user(points, address, words * sizeof(WORD)))
        return EIMBA;
    for (i = 0; i < words; i++)
        if (points[i] < -MAX_COORDINATE || points[i] > MAX_COORDINATE)
            return EINVAL;
    invoke(&control, &input, points, output, outpoints);
    return fn == VDI_SERVICE_COLOR ? output[0] : E_OK;
}

static LONG vector_call(UQUAD code, UQUAD handler, UQUAD output)
{
    VDI_SERVICE_VECTOR_RESULT result = { 0 };
    ASPACE *aspace = kproc_user_aspace(run);
    UWORD protection;

    switch (code) {
    case 118: case 125: case 126: case 127: case 134:
        break;
    default:
        return EINVFN;
    }
    if (!kproc_validate_user_write(output, sizeof(result)))
        return EIMBA;
    if (handler && (handler > 0xffffffffULL || !aspace ||
        !aspace_translate(aspace, handler, NULL, &protection) ||
        (protection & (ASPACE_PROT_USER | ASPACE_PROT_EXEC)) !=
                     (ASPACE_PROT_USER | ASPACE_PROT_EXEC)))
        return EIMBA;
    if (code == 118)
        result.tick_ms = Tickcal();
    /* No user registration exists yet, so its old user value is zero.
     * Never expose or replace the live kernel vector. #432 owns delivery. */
    if (!kproc_copy_to_user(output, &result, sizeof(result)))
        return EIMBA;
    return handler ? EINVFN : E_OK;
}

static LONG pixel_call(UQUAD handle, UQUAD x, UQUAD y, UQUAD address)
{
    VDICONTROL control = { 0 };
    WORD points[2], input = 0, output[2], outpoints[12];

    if (x >= (UWORD)linea_vars.V_REZ_HZ || y >= (UWORD)linea_vars.V_REZ_VT)
        return EINVAL;
    if (!kproc_validate_user_write(address, sizeof(output)))
        return EIMBA;
    points[0] = (WORD)x;
    points[1] = (WORD)y;
    control.code = 105;
    control.handle = (WORD)handle;
    control.nptsin = 1;
    invoke(&control, &input, points, output, outpoints);
    return kproc_copy_to_user(address, output, sizeof(output)) ? E_OK : EIMBA;
}

LONG vdi_service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    if (!kproc_user_aspace(run))
        return EACCDN;
    if (fn == VDI_SERVICE_OPEN)
        return open_workstation(a, b);
    if (fn > VDI_SERVICE_PIXEL)
        return EINVFN;
    if (!owned(a))
        return EIHNDL;
    switch (fn) {
    case VDI_SERVICE_CLOSE:
        close_handle((WORD)a);
        physical_close_if_idle();
        return E_OK;
    case VDI_SERVICE_LINE:
    case VDI_SERVICE_CLIP:
    case VDI_SERVICE_COLOR:
        return coordinate_call(fn, a, b, c);
    case VDI_SERVICE_CHECK:
        return E_OK;
    case VDI_SERVICE_VECTOR:
        return vector_call(b, c, d);
    case VDI_SERVICE_PIXEL:
        return pixel_call(a, b, c, d);
    default:
        return EINVFN;
    }
}
