/* Opaque architecture FPU image used by the existing FSAVE/restore backend. */
#ifndef M68K_FPUCTX_H
#define M68K_FPUCTX_H
#include "portab.h"
#include <stddef.h>
struct fpuctx {
    UBYTE frame[216]; /* Maximum supported 68881/2 frame; 040/060 use less. */
    ULONG ctl[3]; /* FPCR, FPSR, FPIAR */
    UBYTE regs[96]; /* Eight extended-precision FP registers. */
};
typedef char m68k_fpuctx_layout[sizeof(struct fpuctx)==324 &&
    offsetof(struct fpuctx,ctl)==216 && offsetof(struct fpuctx,regs)==228 ? 1:-1];
void m68k_fpu_save(struct fpuctx *ctx);
void m68k_fpu_restore(const struct fpuctx *ctx);
#endif
