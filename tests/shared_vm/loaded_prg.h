#ifndef TEST_LOADED_PRG_H
#define TEST_LOADED_PRG_H
#include <mint/basepage.h>
#include <string.h>

/* A loader's pattern on TOS: Pexec(5/7) makes a basepage and TPA, the
 * loader puts the program's code in that TPA and starts it with Pexec(4),
 * 104, 6 or 106. The child is entered at p_tbase as FreeMiNT does: A0=0, the
 * basepage at 4(sp) and sp at p_hitpa - 0x28, in the TPA (#1018). */
extern const char loaded_prg[],loaded_prg_end[];
__asm__(
    "    .text\n"
    "    .globl loaded_prg\n"
    "    .globl loaded_prg_end\n"
    "loaded_prg:\n"
    "    moveq   #0x11,%d1\n"
    "    cmpa.l  #0,%a0\n"
    "    bne.s   1f\n"
    "    moveq   #0x12,%d1\n"
    "    movea.l 4(%sp),%a1\n"
    "    lea     loaded_prg(%pc),%a2\n"
    "    cmpa.l  8(%a1),%a2\n"           /* p_tbase */
    "    bne.s   1f\n"
    "    moveq   #0x14,%d1\n"
    "    move.l  %sp,0x100+0x64(%a1)\n"     /* for the caller's diagnostics */
    "    move.l  %sp,%d0\n"
    "    addi.l  #0x28,%d0\n"
    "    cmp.l   4(%a1),%d0\n"            /* sp = p_hitpa - 0x28, as on FreeMiNT */
    "    bne.s   1f\n"
    "    moveq   #0x13,%d1\n"
    "    move.l  %a1,0x100+0x60(%a1)\n"   /* the TPA is the child's to write */
    "    cmpa.l  0x100+0x60(%a1),%a1\n"
    "    bne.s   1f\n"
    "    moveq   #0x2b,%d1\n"
    "1:  move.w  %d1,-(%sp)\n"
    "    move.w  #0x4c,-(%sp)\n"
    "    trap    #1\n"
    "loaded_prg_end:\n");
/* p_tbase and the stack/TPA witness are positioned exactly as for a loaded PRG. */
static int loaded_prg_stage(BASEPAGE *bp)
{
    long size=loaded_prg_end-loaded_prg;
    char *text=(char *)bp+256;

    if (bp->p_hitpa-(char *)bp<0x1000 || size<=0 || size>0x60) return 0;
    memcpy(text,loaded_prg,size);
    bp->p_tbase=text;bp->p_tlen=size;
    bp->p_dbase=text+size;bp->p_dlen=0;
    bp->p_bbase=text+size;bp->p_blen=0x40;
    *(long *)(text+0x60)=0;
    *(long *)(text+0x64)=0;
    return 1;
}
#endif
