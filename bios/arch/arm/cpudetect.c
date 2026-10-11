/*
 * cpudetect.c - ARM processor identification
 *
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "processor.h"
#include "processor_arm.h"

static char const arm_unknown[] = "ARM (unknown)";

static struct {
    uint32_t match;
    uint32_t mask;
    const char *name;
} const arm_cputypes[] = {
    { 0x00000000, 0x0008f000, arm_unknown },
    { 0x00807000, 0x0088f000, "ARMv4T" },
    { 0x00007000, 0x0088f000, "ARMv3" },
    { 0x00000000, 0x000f0000, "ARMv3" },
    { 0x00010000, 0x000f0000, "ARMv4" },
    { 0x00020000, 0x000f0000, "ARMv4T" },
    { 0x00030000, 0x000f0000, "ARMv5" },
    { 0x00040000, 0x000f0000, "ARMv5T" },
    { 0x00050000, 0x000f0000, "ARMv5TE" },
    { 0x00060000, 0x000f0000, "ARMv5TEJ" },
    { 0x00070000, 0x000f0000, "ARMv6" },
    { 0x00080000, 0x000f0000, "ARMv7" },
    { 0x00090000, 0x000f0000, "ARMv7M" },
    { 0x41000000, 0xff00f000, "ARMv4" },
    { 0x41007000, 0xfff8fe00, "ARM710" },
    { 0x41807200, 0xffffff00, "ARM720T" },
    { 0x41007400, 0xff00ff00, "ARM74x" },
    { 0x41009400, 0xff00ff00, "ARM94x" },
    { 0x41069260, 0xff0ffff0, "ARM926EJ-S" },
    { 0x41007000, 0x0000f000, "ARM7xx" },
    { 0x4100b360, 0xff00fff0, "ARM1136" },
    { 0x4100b560, 0xff00fff0, "ARM1156" },
    { 0x4100b760, 0xff00fff0, "ARM1176" },
    { 0x4100b020, 0xff00fff0, "ARM11MPCORE" },
    { 0x4401a100, 0xffffffe0, "StrongARM 1100" },
    { 0x6901b110, 0xfffffff0, "StrongARM 1110" },
    { 0x56056900, 0xffffff00, "PXA9xx" },
    { 0x56158000, 0xfffff000, "PXA168" },
    { 0x56050000, 0xff0f0000, "Feroceon" },
    { 0x66015261, 0xff01fff1, "FA526" },
    { 0x410fc050, 0xff0ffff0, "Cortex-A5" },
    { 0x410fc070, 0xff0ffff0, "Cortex-A7" },
    { 0x410fc080, 0xff0ffff0, "Cortex-A8" },
    { 0x410fc090, 0xff0ffff0, "Cortex-A9" },
    { 0x410fc0d0, 0xff0ffff0, "Cortex-A12" },
    { 0x410fc0f0, 0xff0ffff0, "Cortex-A15" },
    { 0x410fc0e0, 0xff0ffff0, "Cortex-A17" },
    { 0x410fd030, 0xff0ffff0, "Cortex-A53" },
    { 0x410fd040, 0xff0ffff0, "Cortex-A35" },
    { 0x410fd050, 0xff0ffff0, "Cortex-A55" },
    { 0x410fd070, 0xff0ffff0, "Cortex-A57" },
    { 0x410fd080, 0xff0ffff0, "Cortex-A72" },
    { 0x410fd090, 0xff0ffff0, "Cortex-A73" },
    { 0x410fd0a0, 0xff0ffff0, "Cortex-A75" },
    { 0x510f02d0, 0xff0ffff0, "Scorpion (dual-core)" },
    { 0x510f04d0, 0xff0ffff0, "Krait (dual-core)" },
    { 0x510f06f0, 0xff0ffff0, "Krait (quad-core)" },
    { 0x510f8030, 0xff0ffff0, "Kryo 385 Silver (Cortex-A55)" },
    { 0x510f8020, 0xff0ffff0, "Kryo 385 Gold (Cortex-A75)" },
    { 0x000f0000, 0x000f0000, "ARMv8" },
};

void detect_cpu(void)
{
    int i;

    mcpu = read_cpuid_id();
    mcpu_name = arm_unknown;
    for (i = 0; i < (int)ARRAY_SIZE(arm_cputypes); i++)
    {
        if ((mcpu & arm_cputypes[i].mask) == arm_cputypes[i].match)
        {
            mcpu_name = arm_cputypes[i].name;
            break;
        }
    }
}
