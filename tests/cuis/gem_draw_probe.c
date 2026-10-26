#include "vdi_service.h"
#include "gem_draw_probe.h"
static LONG service(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)VDI_SERVICE_CLASS << 32) | fn;
    register UQUAD fourth __asm__("r10") = d;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b), "d"(c), "r"(fourth)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
LONG gem_probe_open(WORD *handle, WORD *width, WORD *height)
{
    VDI_SERVICE_WORKSTATION ws;
    WORD input[11] = { 1,1,1,1,1,1,1,0,1,1,2 };
    LONG result = service(VDI_SERVICE_OPEN,(uintptr_t)input,(uintptr_t)&ws,0,0);
    if (!result) { *handle = ws.handle; *width = ws.intout[0]+1; *height = ws.intout[1]+1; }
    return result;
}
LONG gem_probe_close(WORD handle) { return service(VDI_SERVICE_CLOSE,handle,0,0,0); }
int gem_probe_pixel(WORD handle, WORD x, WORD y)
{
    WORD output[2];
    return service(VDI_SERVICE_PIXEL,handle,x,y,(uintptr_t)output) == 0 ? output[1] : -1;
}
