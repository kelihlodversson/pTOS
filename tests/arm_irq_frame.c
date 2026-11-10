/* Production validation with synthetic frames; not native CPU evidence. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#define __ARM_PCS_VFP 1
#define __ARM_NEON 1
#include "arch/arm/irqframe.h"
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void)
{
    ARM_USER_IRQ_FRAME f={0};
    ARM_IRQ_BINDING b={0};
    CHECK(ARM_IRQ_BYTES==352 && sizeof(f)==352);
    b.root=b.expected_root=0x10000;
    b.stack_base=0x81000000; b.stack_top=0x81004000;
    b.frame_address=b.stack_top-sizeof(f);
    f.cpsr=0x60000010; f.pc=0x400000; f.user_sp=0x700000;
    f.irq_lr=f.pc; f.irq_sp=0x82000000; f.svc_sp=b.stack_top;
    f.svc_lr=f.saved_svc_lr=0x80001000;
    CHECK(arm_irq_user_valid(&f,&b,0x80000000));
    f.cpsr=0x13; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.cpsr=0x10;
    f.cpsr|=0x20; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.cpsr=0x10;
    f.pc++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.pc--;
    f.user_sp=0; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.user_sp=0x700000;
    f.user_sp++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.user_sp--;
    f.irq_lr++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.irq_lr--;
    /* SRS writes SVC storage: IRQ SP is captured but never used as a stack. */
    f.irq_sp=0; CHECK(arm_irq_user_valid(&f,&b,0x80000000));
    f.svc_sp-=8; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.svc_sp+=8;
    f.svc_lr++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.svc_lr--;
    b.root++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); b.root--;
    b.frame_address-=8; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); b.frame_address+=8;
    b.stack_base=b.frame_address+8; CHECK(!arm_irq_user_valid(&f,&b,0x80000000));
    b.stack_base=0x81000000; CHECK(arm_irq_user_valid(&f,&b,0x80000000));
    puts("ARM IRQ frame offsets, user/banked validation and MMU/stack binding: PASS (host synthetic inputs)");
    return 0;
}
