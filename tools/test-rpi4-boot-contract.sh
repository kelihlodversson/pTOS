#!/bin/sh
set -eu

startup=bios/machine/raspi/startup.S
memory=bios/machine/raspi/memory.c

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
