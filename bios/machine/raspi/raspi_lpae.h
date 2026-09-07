/*
 * raspi_lpae.h - RPi4 ARMv7 LPAE bootstrap mapping
 */

#ifndef RASPI_LPAE_H
#define RASPI_LPAE_H

#define RASPI_LPAE_TABLE_SIZE  0x5000UL

void raspi_lpae_init_mmu(ULONG memory_size, ULONG table_base);

#endif /* RASPI_LPAE_H */
