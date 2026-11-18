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
    b.stack_base=0x81000000; b.tc=0x8000; b.roots[0]=b.roots[1]=b.root;
    CHECK(m68k_irq_return_controls_valid(&b));
    b.tc++; CHECK(!m68k_irq_return_controls_valid(&b)); b.tc--;
    b.roots[1]++; CHECK(!m68k_irq_return_controls_valid(&b)); b.roots[1]--;
    {
        M68K_USER_IRQ_CAPTURE c={0};
        M68K_IRQ_BINDING changed;
        b.frame_address=(uintptr_t)&f; b.stack_base=b.frame_address-0x1000;
        b.stack_top=b.frame_address+sizeof(f);
        c.image=f; c.binding=b; c.valid=TRUE; c.execution=9; c.sequence=3; c.tid=7;
        CHECK(m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000));
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,10,3,7,0,0x80000000));
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,4,7,0,0x80000000));
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,8,0,0x80000000));
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,1,0x80000000));
        changed=b; changed.tt[2]^=1;
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&changed,9,3,7,0,0x80000000));
        f.a[6]++; CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); f.a[6]--;
        f.format_vector|=0x1000;
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); f.format_vector&=0xfff;
        c.valid=FALSE; CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); c.valid=TRUE;
        b.fpu_type=0x80000; c.binding=b;
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); /* no complete FPU witness */
        c.complete_fpu=TRUE;
        CHECK(m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000));
        c.fpu.frame[0]=0x41;
        CHECK(m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000));
        c.fpu.frame[1]=0x60;
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); /* busy */
        c.fpu.frame[1]=0x30;
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); /* unimplemented */
        c.fpu.frame[1]=0; c.fpu.frame[0]=0x1f;
        CHECK(!m68k_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); /* foreign FPU */
        b.model=60; b.fpu_type=0x100000; c.fpu.frame[0]=0; c.fpu.frame[2]=0x60;
        CHECK(!m68k_irq_fpu_idle(&b,&c.fpu)); /* 060 restart not yet qualified */
        b.model=30; b.fpu_type=0x60000;
        CHECK(!m68k_irq_fpu_idle(&b,&c.fpu)); /* 68882 restart not yet qualified */
        b.tc=0x80c07760UL; b.roots[0]=0x7fff0003UL; b.roots[1]=b.root;
        CHECK(m68k_irq_return_controls_valid(&b));
        b.tc|=0x02000000UL; CHECK(!m68k_irq_return_controls_valid(&b)); b.tc=0x80c07760UL;
        b.roots[0]--; CHECK(!m68k_irq_return_controls_valid(&b)); b.roots[0]++;
        b.tt[0]=0x8000; CHECK(!m68k_irq_return_controls_valid(&b));
    }
    puts("m68k IRQ format-0 offsets, model rejection and MMU/stack binding: PASS (host synthetic inputs)");
    return 0;
}
