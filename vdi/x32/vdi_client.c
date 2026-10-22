/*
 * vdi_client.c - checked, typed VDI entry running in x32 user mode
 *
 * Copyright (C) 2026 The pTOS development team.
 * This file is distributed under the GPL, version 2 or any later version.
 */
#include "vdipb.h"
#include "vdi_service.h"
#include "gemerror.h"

static QUAD syscall4(ULONG cls, ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD rax __asm__("rax") = ((UQUAD)cls << 32) | fn;
    register UQUAD rdi __asm__("rdi") = a;
    register UQUAD rsi __asm__("rsi") = b;
    register UQUAD rdx __asm__("rdx") = c;
    register UQUAD r10 __asm__("r10") = d;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (QUAD)rax;
}

static BOOL accessible(const void *p, ULONG bytes, BOOL write)
{
    if (!bytes)
        return TRUE;
    /* Maccess asks about the caller's own mappings, lazy reservations
     * included. Its mode 0 is read/write; mode 1 is read-only. */
    return syscall4(1, 0x17d, (uintptr_t)p, bytes, write ? 0 : 1, 0) == 0;
}

static LONG service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    return (LONG)syscall4(VDI_SERVICE_CLASS, fn, a, b, c, d);
}

static BOOL zero_tail(const VDI_USER_CONTROL *control, ULONG used)
{
    const UBYTE *p = (const UBYTE *)&control->args;

    for (; used < sizeof(control->args); used++)
        if (p[used])
            return FALSE;
    return TRUE;
}

static LONG memory_raster(VDI_SERVICE_RASTER_REQUEST *request)
{
    ULONG source_bytes, destination_bytes, i;
    UBYTE *source, *destination;
    const UBYTE *original_source;
    UBYTE *original_destination;
    QUAD allocation;
    LONG rc;

    if (request->operation == 109 && !request->source.fd_addr &&
        !request->destination.fd_addr) {
        VDI_SERVICE_SCREEN_REQUEST screen;

        screen.mode = request->mode;
        screen.reserved = request->reserved;
        for (i = 0; i < 8; i++)
            screen.xy[i] = request->xy[i];
        return service(VDI_SERVICE_SCREEN_COPY, (UWORD)request->handle,
                       (uintptr_t)&screen, 0, 0);
    }

    rc = service(VDI_SERVICE_CHECK, (UWORD)request->handle, 0, 0, 0);
    if (rc)
        return rc;
    rc = vdi_user_raster_shape(request, &source_bytes, &destination_bytes);
    if (rc)
        return rc;
    original_source = request->source.fd_addr;
    original_destination = request->destination.fd_addr;
    if (!accessible(original_source, source_bytes, FALSE) ||
        !accessible(original_destination, destination_bytes, TRUE))
        return EIMBA;
    allocation = syscall4(1, 0x48, source_bytes + destination_bytes, 0, 0, 0);
    if (allocation <= 0)
        return ENSMEM;
    source = (UBYTE *)(uintptr_t)allocation;
    destination = source + source_bytes;
    for (i = 0; i < source_bytes; i++)
        source[i] = original_source[i];
    for (i = 0; i < destination_bytes; i++)
        destination[i] = original_destination[i];
    vdi_user_raster_execute(request, source, destination);
    for (i = 0; i < destination_bytes; i++)
        original_destination[i] = destination[i];
    syscall4(1, 0x49, (uintptr_t)source, 0, 0, 0);
    return E_OK;
}

static LONG draw(VDIPB *pb, VDICONTROL *control)
{
    VDI_SERVICE_DRAW_REQUEST request = { 0 };
    VDI_SERVICE_DRAW_RESULT result;
    WORD ni, np;
    LONG i, rc;

    if (!zero_tail(control, 0) || control->nintin > VDI_SERVICE_MAX_TEXT ||
        control->nptsin > 2)
        return EINVAL;
    if ((control->nintin && ((uintptr_t)pb->intin & 1)) ||
        (control->nptsin && ((uintptr_t)pb->ptsin & 1)) ||
        !accessible(pb->intin, control->nintin * sizeof(WORD), FALSE) ||
        !accessible(pb->ptsin, control->nptsin * 2 * sizeof(WORD), FALSE))
        return EIMBA;
    request.code = control->code;
    request.subcode = control->subcode;
    request.nintin = control->nintin;
    request.nptsin = control->nptsin;
    for (i = 0; i < request.nintin; i++)
        request.intin[i] = pb->intin[i];
    for (i = 0; i < request.nptsin * 2; i++)
        request.ptsin[i] = pb->ptsin[i];
    rc = vdi_user_draw_shape(&request, &ni, &np);
    if (rc)
        return rc;
    if ((ni && ((uintptr_t)pb->intout & 1)) ||
        (np && ((uintptr_t)pb->ptsout & 1)) ||
        !accessible(pb->intout, ni * sizeof(WORD), TRUE) ||
        !accessible(pb->ptsout, np * 2 * sizeof(WORD), TRUE))
        return EIMBA;
    rc = service(VDI_SERVICE_DRAW, (UWORD)control->handle,
                 (uintptr_t)&request, (uintptr_t)&result, 0);
    if (rc)
        return rc;
    for (i = 0; i < ni; i++)
        pb->intout[i] = result.intout[i];
    for (i = 0; i < np * 2; i++)
        pb->ptsout[i] = result.ptsout[i];
    control->nintout = ni;
    control->nptsout = np;
    *pb->contrl = *control;
    return E_OK;
}

LONG vdi(VDIPB *parameter_block)
{
    VDIPB pb;
    VDICONTROL control;
    VDI_SERVICE_WORKSTATION workstation;
    VDI_SERVICE_RASTER_REQUEST raster;
    VDI_SERVICE_VECTOR_RESULT vector = { 0 };
    enum vdi_user_kind kind;
    LONG rc;
    ULONG used = 0, handler = 0;
    WORD nintin = 0, nptsin = 0, nintout = 0, nptsout = 0;

    if (((uintptr_t)parameter_block & 3) || !accessible(parameter_block, sizeof(pb), FALSE))
        return EIMBA;
    pb = *parameter_block;
    if (((uintptr_t)pb.contrl & 3) || !accessible(pb.contrl, sizeof(control), TRUE))
        return EIMBA;
    control = *pb.contrl;
    if (control.reserved || control.nintin < 0 || control.nptsin < 0)
        return EINVAL;
    kind = vdi_user_control_kind(control.code, control.subcode);
    if (kind == VDI_USER_UNSUPPORTED || kind == VDI_USER_FONT_LOAD)
        return EINVFN;
    switch (control.code) {
    case 8: case 11: case 12: case 15: case 16: case 22: case 23:
    case 24: case 25: case 32: case 38: case 102: case 113: case 114:
        return draw(&pb, &control);
    case 100:
        nintin = 11;
        nintout = 45;
        nptsout = 6;
        break;
    case 101:
        break;
    case 6:
        if (control.nptsin < 2 || control.nptsin > VDI_SERVICE_MAX_POINTS)
            return EINVAL;
        nptsin = control.nptsin;
        break;
    case 129:
        nintin = 1;
        nptsin = 2;
        break;
    case 17:
        nintin = nintout = 1;
        break;
    case 105:
        nptsin = 1;
        nintout = 2;
        break;
    case 109:
    case 121:
        nintin = control.code == 109 ? 1 : 3;
        nptsin = 4;
        /* fall through */
    case 110:
        used = 8;
        break;
    case 118:
        nintout = 1;
        /* fall through */
    case 125: case 126: case 127: case 134:
        used = 8;
        break;
    default:
        return EINVFN;
    }
    if (!zero_tail(&control, used) || control.nintin != nintin ||
        control.nptsin != nptsin)
        return EINVAL;
    if ((nintin && ((uintptr_t)pb.intin & 1)) ||
        (nptsin && ((uintptr_t)pb.ptsin & 1)) ||
        (nintout && ((uintptr_t)pb.intout & 1)) ||
        (nptsout && ((uintptr_t)pb.ptsout & 1)) ||
        !accessible(pb.intin, (ULONG)nintin * sizeof(WORD), FALSE) ||
        !accessible(pb.ptsin, (ULONG)nptsin * 2 * sizeof(WORD), FALSE) ||
        !accessible(pb.intout, (ULONG)nintout * sizeof(WORD), TRUE) ||
        !accessible(pb.ptsout, (ULONG)nptsout * 2 * sizeof(WORD), TRUE))
        return EIMBA;

    switch (control.code) {
    case 100:
        rc = service(VDI_SERVICE_OPEN, (uintptr_t)pb.intin,
                     (uintptr_t)&workstation, 0, 0);
        if (rc)
            return rc;
        control.handle = workstation.handle;
        {
            int i;

            for (i = 0; i < 45; i++)
                pb.intout[i] = workstation.intout[i];
            for (i = 0; i < 12; i++)
                pb.ptsout[i] = workstation.ptsout[i];
        }
        break;
    case 101:
        rc = service(VDI_SERVICE_CLOSE, (UWORD)control.handle, 0, 0, 0);
        if (!rc)
            control.handle = 0;
        break;
    case 6:
        rc = service(VDI_SERVICE_LINE, (UWORD)control.handle, nptsin,
                     (uintptr_t)pb.ptsin, 0);
        break;
    case 129:
        rc = service(VDI_SERVICE_CLIP, (UWORD)control.handle, (UWORD)pb.intin[0],
                     (uintptr_t)pb.ptsin, 0);
        break;
    case 17:
        rc = service(VDI_SERVICE_COLOR, (UWORD)control.handle, (UWORD)pb.intin[0], 0, 0);
        if (rc >= 0) {
            pb.intout[0] = (WORD)rc;
            rc = E_OK;
        }
        break;
    case 105:
        rc = service(VDI_SERVICE_PIXEL, (UWORD)control.handle, (UWORD)pb.ptsin[0],
                     (UWORD)pb.ptsin[1], (uintptr_t)pb.intout);
        break;
    case 109: case 110: case 121:
        if (((uintptr_t)control.args.raster.source & 3) ||
            ((uintptr_t)control.args.raster.destination & 3) ||
            !accessible(control.args.raster.source, sizeof(VDI_USER_FDB), FALSE) ||
            !accessible(control.args.raster.destination, sizeof(VDI_USER_FDB), control.code == 110))
            return EIMBA;
        raster.handle = control.handle;
        raster.operation = control.code;
        raster.mode = nintin ? pb.intin[0] : 0;
        raster.foreground = control.code == 121 ? pb.intin[1] : 0;
        raster.background = control.code == 121 ? pb.intin[2] : 0;
        raster.reserved = 0;
        {
            int i;

            for (i = 0; i < 8; i++)
                raster.xy[i] = nptsin ? pb.ptsin[i] : 0;
        }
        raster.source = *control.args.raster.source;
        raster.destination = *control.args.raster.destination;
        rc = memory_raster(&raster);
        if (!rc && control.code == 110)
            control.args.raster.destination->fd_stand = raster.destination.fd_stand;
        break;
    default:                            /* typed vector exchange */
        switch (control.code) {
        case 118: handler = (uintptr_t)control.args.timer.new_handler; break;
        case 125: handler = (uintptr_t)control.args.button.new_handler; break;
        case 126: handler = (uintptr_t)control.args.motion.new_handler; break;
        case 127: handler = (uintptr_t)control.args.cursor.new_handler; break;
        case 134: handler = (uintptr_t)control.args.wheel.new_handler; break;
        }
        rc = service(VDI_SERVICE_VECTOR, (UWORD)control.handle, control.code,
                     handler, (uintptr_t)&vector);
        if (rc != E_OK && rc != EINVFN)
            return rc;
        switch (control.code) {
        case 118:
            control.args.timer.old_handler = (VDI_TIMER_CALLBACK *)(uintptr_t)vector.old_handler;
            pb.intout[0] = vector.tick_ms;
            break;
        case 125: control.args.button.old_handler = (VDI_BUTTON_CALLBACK *)(uintptr_t)vector.old_handler; break;
        case 126: control.args.motion.old_handler = (VDI_MOTION_CALLBACK *)(uintptr_t)vector.old_handler; break;
        case 127: control.args.cursor.old_handler = (VDI_CURSOR_CALLBACK *)(uintptr_t)vector.old_handler; break;
        case 134: control.args.wheel.old_handler = (VDI_WHEEL_CALLBACK *)(uintptr_t)vector.old_handler; break;
        }
        break;
    }
    if (rc && rc != EINVFN)
        return rc;
    control.nintout = nintout;
    control.nptsout = nptsout;
    *pb.contrl = control;
    return rc;
}
