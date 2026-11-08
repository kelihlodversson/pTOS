/* Production short-frame admission with synthetic input; not CPU execution. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "arch/m68k/irqframe.h"
#include "arch/m68k/userentry.h"
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void)
{
    M68K_USER_IRQ_FRAME f={0};
    M68K_IRQ_BINDING b={0};
    UWORD format;
    ULONG fpu;
    ULONG model;
    b.root=b.expected_root=0x10000; b.stack_base=0x81000000; b.stack_top=0x81004000;
    b.frame_address=b.stack_top-sizeof(f); f.usp=0x700000; f.pc=0x400000;
    f.format_vector=30*4;
    for (model=30;model<=60;model+=10) {
        if (model==50) continue;
        b.model=model; CHECK(m68k_irq_user_valid(&f,&b,0x80000000));
        for (fpu=0;fpu<=0x110000UL;fpu+=0x10000UL) {
            BOOL supported=!fpu || (model==30 && (fpu==0x20000UL || fpu==0x40000UL || fpu==0x60000UL)) ||
                (model==40 && fpu==0x80000UL) || (model==60 && fpu==0x100000UL);
            b.fpu_type=fpu;
            CHECK(m68k_irq_user_valid(&f,&b,0x80000000)==supported);
        }
        b.fpu_type=0;
        for (format=1;format<16;format++) {
            f.format_vector=(format<<12)|30*4;
            CHECK(!m68k_irq_user_valid(&f,&b,0x80000000));
        }
        f.format_vector=30*4;
    }
    f.format_vector=4*4; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000));
    f.format_vector=256*4; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000));
    f.format_vector=30*4;
    b.model=20; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); b.model=40;
    f.sr=0x2000; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); f.sr=0;
    f.sr=0x1000; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); f.sr=0;
    f.format_vector++; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); f.format_vector--;
    f.pc++; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); f.pc--;
    f.usp=0; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); f.usp=0x700000;
    b.root++; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); b.root--;
    b.frame_address-=2; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000)); b.frame_address+=2;
    b.stack_base=b.frame_address+2; CHECK(!m68k_irq_user_valid(&f,&b,0x80000000));
    puts("m68k IRQ format-0 offsets, model rejection and MMU/stack binding: PASS (host synthetic inputs)");
    return 0;
}
