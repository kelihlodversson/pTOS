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
    static ARM_USER_IRQ_FRAME f;
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
    /* AAPCS requires eight bytes at public call boundaries, but permits
     * word alignment inside a leaf. A hardware IRQ can land there. */
    f.user_sp+=4; CHECK(arm_irq_user_valid(&f,&b,0x80000000));
    f.user_sp+=2; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.user_sp-=6;
    f.irq_lr++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.irq_lr--;
    /* SRS writes SVC storage: IRQ SP is captured but never used as a stack. */
    f.irq_sp=0; CHECK(arm_irq_user_valid(&f,&b,0x80000000));
    f.svc_sp-=8; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.svc_sp+=8;
    f.svc_lr++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); f.svc_lr--;
    b.root++; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); b.root--;
    b.frame_address-=8; CHECK(!arm_irq_user_valid(&f,&b,0x80000000)); b.frame_address+=8;
    b.stack_base=b.frame_address+8; CHECK(!arm_irq_user_valid(&f,&b,0x80000000));
    b.stack_base=0x81000000; CHECK(arm_irq_user_valid(&f,&b,0x80000000));
    b.sctlr=1; b.ttbcr=1; b.ttbr0=b.root; b.ttbr1=0x20000;
    b.cpacr=0x00f00000; b.fpexc=0x40000000;
    CHECK(arm_irq_return_controls_valid(&f,&b));
    f.cpsr|=1UL<<24; CHECK(!arm_irq_return_controls_valid(&f,&b)); f.cpsr=0x10;
    f.cpsr|=1UL<<10; CHECK(!arm_irq_return_controls_valid(&f,&b)); f.cpsr=0x10;
    f.cpsr|=1UL<<25; CHECK(!arm_irq_return_controls_valid(&f,&b)); f.cpsr=0x10;
    b.fpexc|=0x80000000UL; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.fpexc=0x40000000;
    b.fpexc|=0x10000000UL; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.fpexc=0x40000000;
    b.sctlr=0; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.sctlr=1;
    b.sctlr|=1UL<<25; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.sctlr=1;
    b.ttbcr|=0x80000000UL; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.ttbcr=1;
    b.ttbr0+=0x2000; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.ttbr0=b.root;
    b.cpacr=0; CHECK(!arm_irq_return_controls_valid(&f,&b)); b.cpacr=0x00f00000;
    {
        ARM_USER_IRQ_CAPTURE c={0};
        ARM_IRQ_BINDING changed;
        b.frame_address=(uintptr_t)&f; b.stack_base=b.frame_address-0x1000;
        b.stack_top=b.frame_address+sizeof(f); f.svc_sp=b.stack_top;
        /* Non-PIE static fixture keeps modeled 32-bit banked SP representable. */
        if ((uintptr_t)f.svc_sp==b.stack_top) {
            c.image=f; c.binding=b; c.valid=TRUE; c.execution=9; c.sequence=3; c.tid=7;
            CHECK(arm_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000));
            CHECK(!arm_irq_same_entry_valid(&c,&f,&b,10,3,7,0,0x80000000));
            CHECK(!arm_irq_same_entry_valid(&c,&f,&b,9,4,7,0,0x80000000));
            CHECK(!arm_irq_same_entry_valid(&c,&f,&b,9,3,8,0,0x80000000));
            CHECK(!arm_irq_same_entry_valid(&c,&f,&b,9,3,7,1,0x80000000));
            changed=b; changed.dacr^=1;
            CHECK(!arm_irq_same_entry_valid(&c,&f,&changed,9,3,7,0,0x80000000));
            f.r[12]++; CHECK(!arm_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); f.r[12]--;
            f.vfp[2]++; CHECK(!arm_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000)); f.vfp[2]--;
            c.valid=FALSE; CHECK(!arm_irq_same_entry_valid(&c,&f,&b,9,3,7,0,0x80000000));
        } else abort();
    }
    puts("ARM IRQ frame offsets, user/banked validation and MMU/stack binding: PASS (host synthetic inputs)");
    return 0;
}
