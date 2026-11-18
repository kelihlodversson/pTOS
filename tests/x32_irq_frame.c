/* Production frame validation with synthetic inputs, no privileged execution. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "arch/x86_64/irqframe.h"
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void)
{
    X86_USER_IRQ_FRAME f={0};
    X86_IRQ_BINDING b={0};
    X86_IRQ_CONTROLS *c=&b.controls;
    b.cr3=b.expected_cr3=0x1000; b.syscall_top=0x200ff8;
    b.irq_top=0x201000; b.frame_address=b.irq_top-X86_IRQ_BYTES;
    f.cs=X86_64_USER_CODE_SEL; f.ss=X86_64_USER_DATA_SEL;
    f.rip=0x400000; f.rsp=0x700000; f.rflags=0x202; f.vector=32;
    CHECK(x86_irq_user_valid(&f,&b,0x80000000));
    c->cr0=0x80000001ULL; c->cr4=1ULL<<9; c->efer=0xc01;
    c->kernel_gs_base=c->entry_record=0xffff800000100000ULL;
    CHECK(x86_irq_return_controls_valid(c));
    c->cr0|=4; CHECK(!x86_irq_return_controls_valid(c)); c->cr0&=~4ULL;
    c->cr0|=8; CHECK(!x86_irq_return_controls_valid(c)); c->cr0&=~8ULL;
    c->cr4|=1ULL<<18; CHECK(!x86_irq_return_controls_valid(c)); c->cr4&=~(1ULL<<18);
    c->cr4|=1ULL<<16; CHECK(!x86_irq_return_controls_valid(c)); c->cr4&=~(1ULL<<16);
    c->efer=0x401; CHECK(!x86_irq_return_controls_valid(c)); c->efer=0xc01;
    c->fs_base=1; CHECK(!x86_irq_return_controls_valid(c)); c->fs_base=0;
    c->gs_base=1; CHECK(!x86_irq_return_controls_valid(c)); c->gs_base=0;
    c->fs=0x23; CHECK(!x86_irq_return_controls_valid(c)); c->fs=0;
    c->gs=0x23; CHECK(!x86_irq_return_controls_valid(c)); c->gs=0;
    c->in_syscall=TRUE; CHECK(!x86_irq_return_controls_valid(c)); c->in_syscall=FALSE;
    c->kernel_gs_base++; CHECK(!x86_irq_return_controls_valid(c)); c->kernel_gs_base--;
    c->ds=8; CHECK(!x86_irq_return_controls_valid(c)); c->ds=X86_64_USER_DATA_SEL;
    CHECK(x86_irq_return_controls_valid(c));
    f.cs=8; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.cs=X86_64_USER_CODE_SEL;
    f.ss=0; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.ss=X86_64_USER_DATA_SEL;
    f.rip=0x80000000; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.rip=0x400000;
    f.rsp=0; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.rsp=0x700000;
    f.rflags|=0x3000; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.rflags=0x202;
    f.rflags|=1UL<<14; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.rflags=0x202;
    f.rflags|=1UL<<17; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.rflags=0x200;
    CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.rflags=0x202;
    b.expected_cr3=0x2000; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); b.expected_cr3=0x1000;
    b.syscall_top-=8; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); b.syscall_top+=8;
    b.frame_address-=16; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); b.frame_address+=16;
    b.frame_address++; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); b.frame_address--;
    f.padding=1; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.padding=0;
    f.vector=14; CHECK(!x86_irq_user_valid(&f,&b,0x80000000)); f.vector=44;
    CHECK(x86_irq_user_valid(&f,&b,0x80000000));
    puts("x32 IRQ frame offsets, user/control validation and CR3/stack binding: PASS (host synthetic inputs)");
    return 0;
}
