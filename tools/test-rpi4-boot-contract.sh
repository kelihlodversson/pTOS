#!/bin/sh
set -eu

startup=bios/machine/raspi/startup.S
memory=bios/machine/raspi/memory.c
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

invalidate_line=$(grep -n 'invalidate_data_cache_all();' "$memory" | cut -d: -f1)
descriptor_line=$(grep -n 'entry->Value10 = 2;' "$memory" | cut -d: -f1)
flush_line=$(grep -n 'flush_data_cache_all();' "$memory" | cut -d: -f1 | awk 'END { print }')
mmu_line=$(grep -n 'mcr p15, 0, %0, c1, c0,  0' "$memory" | tail -n 1 | cut -d: -f1)

test "$invalidate_line" -lt "$descriptor_line"
test "$descriptor_line" -lt "$flush_line"
test "$flush_line" -lt "$mmu_line"

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
