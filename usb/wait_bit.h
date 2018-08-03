/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Wait for bit with timeout and ctrlc.
 * Adapted from the u-boot sources for EmuTOS
 *
 * (C) Copyright 2015 Mateusz Kulikowski <mateusz.kulikowski@gmail.com>
 */

#ifndef __WAIT_BIT_H
#define __WAIT_BIT_H

#include "portab.h"
#include "usb_io.h"
#include "../bios/raspi_int.h"

/**
 * wait_for_bit_x()     waits for bit set/cleared in register
 *
 * Function polls register waiting for specific bit(s) change
 * (either 0->1 or 1->0). It can fail under two conditions:
 * - Timeout
 * - User interaction (CTRL-C)
 * Function succeeds only if all bits of masked register are set/cleared
 * (depending on set option).
 *
 * @param reg           Register that will be read (using read_x())
 * @param mask          Bit(s) of register that must be active
 * @param set           Selects wait condition (bit set or clear)
 * @param timeout_ms    Timeout (in milliseconds)
 * @param breakable     Enables CTRL-C interruption
 * @return              0 on success, ETIMEDOUT or EINTR on failure
 */

#define cpu2dummy8(x) (x)

#define BUILD_WAIT_FOR_BIT(endian, size)                                \
                                                                        \
static inline int wait_for_bit_##endian##size(const void *reg,          \
                                     const uint##size##_t mask,         \
                                     const BOOL set,                    \
                                     const unsigned int timeout_ms)     \
{                                                                       \
        uint##size##_t val;                                             \
        unsigned long start = raspi_get_timer(0);                       \
                                                                        \
        while (1) {                                                     \
                val = cpu2##endian##size(__read(size,reg));             \
                                                                        \
                if (!set)                                               \
                        val = ~val;                                     \
                                                                        \
                if ((val & mask) == mask)                               \
                        return 0;                                       \
                                                                        \
                if (raspi_get_timer(start) > timeout_ms)                \
                        break;                                          \
                                                                        \
                raspi_delay_us(1);                                      \
        }                                                               \
                                                                        \
        KDEBUG(("%s: Timeout (reg=%p mask=%x wait_set=%i)\n", __func__, \
              reg, mask, set));                                         \
                                                                        \
        return ETIMEDOUT;                                              \
}

BUILD_WAIT_FOR_BIT(dummy, 8)
BUILD_WAIT_FOR_BIT(le, 16)
BUILD_WAIT_FOR_BIT(be, 16)
BUILD_WAIT_FOR_BIT(le, 32)
BUILD_WAIT_FOR_BIT(be, 32)

#endif
