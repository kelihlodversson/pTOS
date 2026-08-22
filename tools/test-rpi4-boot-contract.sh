#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

startup=bios/machine/raspi/startup.S
memory=bios/machine/raspi/memory.c
virt_mmu=bios/machine/virt-arm/virt_mmu.c
gic=bios/machine/raspi/raspi_gic.c
interrupts=bios/raspi_int.c
build=bios/build.mk

awk '
    /^#if defined\(TARGET_RPI4\)/ {
        rpi4 = 1
        next
    }
    rpi4 && /^#define CORE0_MBOX3_SET[[:space:]]+0xff80008C/ {
        rpi4_base = 1
        next
    }
    rpi4_base && /^#else/ {
        fallback = 1
        next
    }
    fallback && /^#define CORE0_MBOX3_SET[[:space:]]+0x4000008C/ {
        fallback_base = 1
    }
    END {
        exit !(rpi4_base && fallback_base)
    }
' "$startup"

handoff_line=$(awk '
    /^static void init_mmu\(ULONG memory_size\)/ { in_init_mmu = 1 }
    in_init_mmu && /flush_data_cache_all\(\);/ { print NR; exit }
' "$memory")
descriptor_line=$(grep -n 'entry->Value10 = 2;' "$memory" | cut -d: -f1)
flush_line=$(grep -n 'flush_data_cache_all();' "$memory" | cut -d: -f1 | awk 'END { print }')
mmu_line=$(grep -n 'mcr p15, 0, %0, c1, c0,  0' "$memory" | tail -n 1 | cut -d: -f1)

test "$handoff_line" -lt "$descriptor_line"
test "$descriptor_line" -lt "$flush_line"
test "$flush_line" -lt "$mmu_line"

virt_first_cache_call=$(awk '
    /^void virt_mmu_bootstrap\(/ { in_bootstrap = 1 }
    in_bootstrap && /cache_all\(\);/ { print; exit }
' "$virt_mmu")
test "$virt_first_cache_call" = '    flush_data_cache_all();'

grep -q '^#if CONF_WITH_RASPI_UART0' "$startup"
grep -q '^#define EARLY_UART0_BASE.*0x20201000' "$startup"
grep -q '^#define EARLY_UART0_BASE.*0x3f201000' "$startup"
grep -q '^#define EARLY_UART0_BASE.*0xfe201000' "$startup"

entry_marker=$(grep -n 'mov r0, #0x45.*entry' "$startup" | cut -d: -f1)
mmu_marker=$(grep -n 'mov r0, #0x4d.*MMU' "$startup" | cut -d: -f1)
return_marker=$(grep -n 'mov r0, #0x52.*return' "$startup" | cut -d: -f1)
mmu_call=$(grep -n 'bl     _raspi_vcmem_init' "$startup" | cut -d: -f1)

test "$entry_marker" -lt "$mmu_marker"
test "$mmu_marker" -lt "$mmu_call"
test "$mmu_call" -lt "$return_marker"

grep -q '^#define RASPI_GIC_DIST_BASE 0xff841000UL' "$gic"
grep -q '^#define RASPI_GIC_CPU_BASE  0xff842000UL' "$gic"

awk '
    /^#if defined\(TARGET_RPI4\)/ {
        rpi4 = 1
        next
    }
    rpi4 && /^#else/ {
        rpi4 = 0
        next
    }
    /^#endif/ {
        rpi4 = 0
    }
    /raspi_gic_init\(\)/ {
        if (rpi4)
            init_in_rpi4++
        else
            init_outside_rpi4++
    }
    /raspi_gic_handle_irq\(\)/ {
        if (rpi4)
            handle_in_rpi4++
        else
            handle_outside_rpi4++
    }
    END {
        exit !(init_in_rpi4 == 1 && !init_outside_rpi4 \
            && handle_in_rpi4 == 1 && !handle_outside_rpi4)
    }
' "$interrupts"

awk '
    /^obj-\$\(TARGET_RPI4\).*raspi_gic\.o/ {
        rpi4 = 1
    }
    /raspi_gic\.o/ && !/^obj-\$\(TARGET_RPI4\).*raspi_gic\.o/ {
        other = 1
    }
    END {
        exit !(rpi4 && !other)
    }
' "$build"

grep -q 'raspi_gic_connect_irq(30, raspi_timer3_handler);' "$interrupts"

awk '
    /^void raspi_init_system_timer\(void\)/ {
        timer_init = 1
        next
    }
    timer_init && /^#if defined\(TARGET_RPI4\)/ {
        rpi4 = 1
        next
    }
    timer_init && rpi4 && /^#else/ {
        rpi4 = 0
        legacy = 1
        next
    }
    timer_init && rpi4 && /cntfrq/ {
        cntfrq = 1
    }
    timer_init && rpi4 && /cntp_cval/ {
        cntp_cval = 1
    }
    timer_init && rpi4 && /CNTP_CTL/ {
        cntp_ctl = 1
    }
    timer_init && rpi4 && /flush_prefetch_buffer\(\)/ {
        isb = 1
    }
    timer_init && rpi4 && /ARM_SYSTIMER.compare\[3\]/ {
        rpi4_systimer = 1
    }
    timer_init && legacy && /ARM_SYSTIMER.compare\[3\]/ {
        legacy_systimer = 1
    }
    END {
        exit !(cntfrq && cntp_cval && cntp_ctl && isb \
            && !rpi4_systimer && legacy_systimer)
    }
' "$interrupts"

awk '
    /^void raspi_timer3_handler\(void\)/ {
        timer_handler = 1
        next
    }
    timer_handler && /^#if defined\(TARGET_RPI4\)/ {
        rpi4 = 1
        next
    }
    timer_handler && rpi4 && /^#else/ {
        rpi4 = 0
    }
    timer_handler && rpi4 && /cntp_cval/ {
        cntp_cval = 1
    }
    END {
        exit !cntp_cval
    }
' "$interrupts"
