/*
 * mman.h - native GEMDOS private mapping ABI (#660)
 *
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PTOS_MMAN_H
#define PTOS_MMAN_H

/* FreeMiNT reserves 0x17e/0x17f for mmap/munmap, without signatures.
 * Mprot at 0x17c is a pTOS extension using a currently unassigned slot. */
#define GEMDOS_MPROT  0x17c
#define GEMDOS_MMAP   0x17e
#define GEMDOS_MUNMAP 0x17f

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_ANON            MAP_ANONYMOUS
#define MAP_FIXED_NOREPLACE 0x100000

#define PTOS_MMAP_BASE  0x01000000UL
#define PTOS_MMAP_LIMIT 0x0f000000UL

/* Native x32 calls, all scalar arguments in syscall slots:
 * Mmap(address, length, protection, flags, fd, offset) -> address or error
 * Munmap(address, length) -> E_OK or error
 * Mprot(address, length, protection) -> E_OK or error
 *
 * MAP_PRIVATE|MAP_ANONYMOUS ignores fd and requires offset 0.
 * MAP_PRIVATE without ANONYMOUS maps readable native FAT files: offset
 * must be page aligned and the range must fit the rounded file extent.
 * Mappings retain the open backing independently of Fclose/unlink. Clean
 * pages share backing; private writes copy before modifying a page.
 * MAP_FIXED is unsupported; MAP_FIXED_NOREPLACE never replaces a mapping.
 * An ordinary address is a hint. Lengths are rounded up to pages. Munmap
 * and Mprot require page-aligned addresses. Mprot supports NONE, R, RW, RX;
 * hardware read access is implied by WRITE/EXEC. W+X is refused.
 * Anonymous pages and the final file-page tail are zero-filled. All
 * mappings are reclaimed on process exit. MAP_SHARED remains unsupported.
 * These are GEMDOS error returns, not libc's MAP_FAILED/errno convention.
 */

#endif
