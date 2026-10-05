/*
 * smbios.h - x86-64 machine name from the firmware's SMBIOS tables
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_SMBIOS_H
#define PC_X86_64_SMBIOS_H

#include "portab.h"

/*
 * Finds the SMBIOS entry point in the EFI configuration table, walks the
 * structure table to the System Information structure (type 1) and copies
 * a short "<manufacturer> <product>" name out of it into this file's own
 * bss.  Must be called while the firmware's memory is still ours to read
 * -- before ExitBootServices(), and before the identity mapping is
 * dropped -- because the tables live in firmware memory this image has no
 * claim on afterwards, which is why the name is copied here and not looked
 * up on demand.  No SMBIOS, or only placeholder strings, is not fatal: it
 * just means x86_64_smbios_machine_name() has nothing to report.
 *
 * system_table is really an EFI_SYSTEM_TABLE* (efi.h) -- taken as void* so
 * this header, unlike efi.h itself, stays includable from bios/machine.c
 * and other shared, non-EFI-aware files.  smbios.c casts it back.
 */
void x86_64_smbios_probe(void *system_table);

/*
 * The name x86_64_smbios_probe() found, e.g. "QEMU Standard PC" or
 * "LENOVO ThinkPad X1", at most X86_64_SMBIOS_NAME_MAX characters (the
 * width of the welcome screen's value column), or NULL if it found none.
 */
#define X86_64_SMBIOS_NAME_MAX 20

const char *x86_64_smbios_machine_name(void);

#endif /* PC_X86_64_SMBIOS_H */
