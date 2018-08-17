//
// raspi_emmc.cpp
//
// Provides an interface to the EMMC controller and commands for interacting
// with an sd card
//
// Copyright (C) 2013 by John Cronin <jncronin@tysos.org>
// Modified for Circle by R. Stange
//
// Modified for pTOS by K. Hlodversson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// References:
//
// PLSS - SD Group Physical Layer Simplified Specification ver 3.00
// HCSS - SD Group Host Controller Simplified Specification ver 3.00
//
// Broadcom BCM2835 ARM Peripherals Guide
//
//#define ENABLE_KDEBUG
//#define EMMC_DEBUG
//#define EMMC_DEBUG2

// Write protect until read support is debugged
#define EMMC_DEBUG_WP

#include "config.h"
#include "portab.h"

#include "disk.h"
#include "endian.h"
#include "asm.h"
#include "blkdev.h"
#include "delay.h"
#include "gemerror.h"
#include "mfp.h"
#include "raspi_emmc.h"
#include "string.h"
#include "tosvars.h"
#include "raspi_io.h"
#include "raspi_mbox.h"
#include "raspi_int.h"

#include "kprint.h"

#ifdef EMMC_DEBUG
#   undef KINFO
#   undef KDEBUG
#   define KINFO(args) cprintf args
#   define KDEBUG(args) cprintf args
#endif

#if CONF_WITH_RASPI_EMMC



//
// According to the BCM2835 ARM Peripherals Guide the EMMC STATUS register
// should not be used for polling. The original driver does not meet this
// specification in this point but the modified driver should do so.
// Define EMMC_POLL_STATUS_REG if you want the original function!
//
//#define EMMC_POLL_STATUS_REG

// Enable 1.8V support
//#define SD_1_8V_SUPPORT

// Enable 4-bit support
#define SD_4BIT_DATA

// SD Clock Frequencies (in Hz)
#define SD_CLOCK_ID         400000
#define SD_CLOCK_NORMAL     25000000
#define SD_CLOCK_HIGH       50000000
#define SD_CLOCK_100        100000000
#define SD_CLOCK_208        208000000

// Enable SDXC maximum performance mode
// Requires 150 mA power so disabled on the RPi for now
#define SDXC_MAXIMUM_PERFORMANCE

// Enable card interrupts
//#define SD_CARD_INTERRUPTS

// Allow old sdhci versions (may cause errors)
// Required for QEMU
#define EMMC_ALLOW_OLD_SDHCI

#define ARM_EMMC_BASE       (ARM_IO_BASE + 0x300000)
#define EMMC_ARG2           (*(volatile ULONG*)(ARM_EMMC_BASE + 0x00))
#define EMMC_BLKSIZECNT     (*(volatile ULONG*)(ARM_EMMC_BASE + 0x04))
#define EMMC_ARG1           (*(volatile ULONG*)(ARM_EMMC_BASE + 0x08))
#define EMMC_CMDTM          (*(volatile ULONG*)(ARM_EMMC_BASE + 0x0C))
#define EMMC_RESP0          (*(volatile ULONG*)(ARM_EMMC_BASE + 0x10))
#define EMMC_RESP1          (*(volatile ULONG*)(ARM_EMMC_BASE + 0x14))
#define EMMC_RESP2          (*(volatile ULONG*)(ARM_EMMC_BASE + 0x18))
#define EMMC_RESP3          (*(volatile ULONG*)(ARM_EMMC_BASE + 0x1C))
#define EMMC_DATA           (*(volatile ULONG*)(ARM_EMMC_BASE + 0x20))
#define EMMC_STATUS         (*(volatile ULONG*)(ARM_EMMC_BASE + 0x24))
#define EMMC_CONTROL0       (*(volatile ULONG*)(ARM_EMMC_BASE + 0x28))
#define EMMC_CONTROL1       (*(volatile ULONG*)(ARM_EMMC_BASE + 0x2C))
#define EMMC_INTERRUPT      (*(volatile ULONG*)(ARM_EMMC_BASE + 0x30))
#define EMMC_IRPT_MASK      (*(volatile ULONG*)(ARM_EMMC_BASE + 0x34))
#define EMMC_IRPT_EN        (*(volatile ULONG*)(ARM_EMMC_BASE + 0x38))
#define EMMC_CONTROL2       (*(volatile ULONG*)(ARM_EMMC_BASE + 0x3C))
#define EMMC_CAPABILITIES_0 (*(volatile ULONG*)(ARM_EMMC_BASE + 0x40))
#define EMMC_CAPABILITIES_1 (*(volatile ULONG*)(ARM_EMMC_BASE + 0x44))
#define EMMC_FORCE_IRPT     (*(volatile ULONG*)(ARM_EMMC_BASE + 0x50))
#define EMMC_BOOT_TIMEOUT   (*(volatile ULONG*)(ARM_EMMC_BASE + 0x70))
#define EMMC_DBG_SEL        (*(volatile ULONG*)(ARM_EMMC_BASE + 0x74))
#define EMMC_EXRDFIFO_CFG   (*(volatile ULONG*)(ARM_EMMC_BASE + 0x80))
#define EMMC_EXRDFIFO_EN    (*(volatile ULONG*)(ARM_EMMC_BASE + 0x84))
#define EMMC_TUNE_STEP      (*(volatile ULONG*)(ARM_EMMC_BASE + 0x88))
#define EMMC_TUNE_STEPS_STD (*(volatile ULONG*)(ARM_EMMC_BASE + 0x8C))
#define EMMC_TUNE_STEPS_DDR (*(volatile ULONG*)(ARM_EMMC_BASE + 0x90))
#define EMMC_SPI_INT_SPT    (*(volatile ULONG*)(ARM_EMMC_BASE + 0xF0))
#define EMMC_SLOTISR_VER    (*(volatile ULONG*)(ARM_EMMC_BASE + 0xFC))

#define ARM_GPIO_BASE		(ARM_IO_BASE + 0x200000)
#define ARM_GPIO_GPFSEL0	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x00))
#define ARM_GPIO_GPFSEL1	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x04))
#define ARM_GPIO_GPFSEL2	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x08))
#define ARM_GPIO_GPFSEL3	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x0c))
#define ARM_GPIO_GPFSEL4	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x10))
#define ARM_GPIO_GPFSEL5	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x14))
#define ARM_GPIO_GPPUD		(*(volatile ULONG*)(ARM_GPIO_BASE + 0x94))
#define ARM_GPIO_GPPUDCLK0	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x98))
#define ARM_GPIO_GPPUDCLK1	(*(volatile ULONG*)(ARM_GPIO_BASE + 0x9c))


#define SD_CMD_INDEX(a)          ((a) << 24)
#define SD_CMD_TYPE_NORMAL       0
#define SD_CMD_TYPE_SUSPEND      (1 << 22)
#define SD_CMD_TYPE_RESUME       (2 << 22)
#define SD_CMD_TYPE_ABORT        (3 << 22)
#define SD_CMD_TYPE_MASK         (3 << 22)
#define SD_CMD_ISDATA            (1 << 21)
#define SD_CMD_IXCHK_EN          (1 << 20)
#define SD_CMD_CRCCHK_EN         (1 << 19)
#define SD_CMD_RSPNS_TYPE_NONE   0           // For no response
#define SD_CMD_RSPNS_TYPE_136    (1 << 16)   // For response R2 (with CRC), R3,4 (no CRC)
#define SD_CMD_RSPNS_TYPE_48     (2 << 16)   // For responses R1, R5, R6, R7 (with CRC)
#define SD_CMD_RSPNS_TYPE_48B    (3 << 16)   // For responses R1b, R5b (with CRC)
#define SD_CMD_RSPNS_TYPE_MASK   (3 << 16)
#define SD_CMD_MULTI_BLOCK       (1 << 5)
#define SD_CMD_DAT_DIR_HC        0
#define SD_CMD_DAT_DIR_CH        (1 << 4)
#define SD_CMD_AUTO_CMD_EN_NONE  0
#define SD_CMD_AUTO_CMD_EN_CMD12 (1 << 2)
#define SD_CMD_AUTO_CMD_EN_CMD23 (2 << 2)
#define SD_CMD_BLKCNT_EN         (1 << 1)
#define SD_CMD_DMA               1

#define SD_ERR_CMD_TIMEOUT       0
#define SD_ERR_CMD_CRC           1
#define SD_ERR_CMD_END_BIT       2
#define SD_ERR_CMD_INDEX         3
#define SD_ERR_DATA_TIMEOUT      4
#define SD_ERR_DATA_CRC          5
#define SD_ERR_DATA_END_BIT      6
#define SD_ERR_CURRENT_LIMIT     7
#define SD_ERR_AUTO_CMD12        8
#define SD_ERR_ADMA              9
#define SD_ERR_TUNING           10
#define SD_ERR_RSVD             11

#define SD_ERR_MASK_CMD_TIMEOUT   (1 << (16 + SD_ERR_CMD_TIMEOUT))
#define SD_ERR_MASK_CMD_CRC       (1 << (16 + SD_ERR_CMD_CRC))
#define SD_ERR_MASK_CMD_END_BIT   (1 << (16 + SD_ERR_CMD_END_BIT))
#define SD_ERR_MASK_CMD_INDEX     (1 << (16 + SD_ERR_CMD_INDEX))
#define SD_ERR_MASK_DATA_TIMEOUT  (1 << (16 + SD_ERR_CMD_TIMEOUT))
#define SD_ERR_MASK_DATA_CRC      (1 << (16 + SD_ERR_CMD_CRC))
#define SD_ERR_MASK_DATA_END_BIT  (1 << (16 + SD_ERR_CMD_END_BIT))
#define SD_ERR_MASK_CURRENT_LIMIT (1 << (16 + SD_ERR_CMD_CURRENT_LIMIT))
#define SD_ERR_MASK_AUTO_CMD12    (1 << (16 + SD_ERR_CMD_AUTO_CMD12))
#define SD_ERR_MASK_ADMA          (1 << (16 + SD_ERR_CMD_ADMA))
#define SD_ERR_MASK_TUNING        (1 << (16 + SD_ERR_CMD_TUNING))

#define SD_COMMAND_COMPLETE       1
#define SD_TRANSFER_COMPLETE      (1 << 1)
#define SD_BLOCK_GAP_EVENT        (1 << 2)
#define SD_DMA_INTERRUPT          (1 << 3)
#define SD_BUFFER_WRITE_READY     (1 << 4)
#define SD_BUFFER_READ_READY      (1 << 5)
#define SD_CARD_INSERTION         (1 << 6)
#define SD_CARD_REMOVAL           (1 << 7)
#define SD_CARD_INTERRUPT         (1 << 8)

#define SD_RESP_NONE              SD_CMD_RSPNS_TYPE_NONE
#define SD_RESP_R1                (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R1b               (SD_CMD_RSPNS_TYPE_48B | SD_CMD_CRCCHK_EN)
#define SD_RESP_R2                (SD_CMD_RSPNS_TYPE_136 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R3                SD_CMD_RSPNS_TYPE_48
#define SD_RESP_R4                SD_CMD_RSPNS_TYPE_136
#define SD_RESP_R5                (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R5b               (SD_CMD_RSPNS_TYPE_48B | SD_CMD_CRCCHK_EN)
#define SD_RESP_R6                (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)
#define SD_RESP_R7                (SD_CMD_RSPNS_TYPE_48 | SD_CMD_CRCCHK_EN)

#define SD_DATA_READ        (SD_CMD_ISDATA | SD_CMD_DAT_DIR_CH)
#define SD_DATA_WRITE       (SD_CMD_ISDATA | SD_CMD_DAT_DIR_HC)

#define SD_CMD_RESERVED(a)  0xffffffff

#define SUCCESS          (card.last_cmd_success)
#define FAIL             (card.last_cmd_success == 0)
#define TIMEOUT          (FAIL && (card.last_error == 0))
#define CMD_TIMEOUT      (FAIL && (card.last_error & (1 << 16)))
#define CMD_CRC          (FAIL && (card.last_error & (1 << 17)))
#define CMD_END_BIT      (FAIL && (card.last_error & (1 << 18)))
#define CMD_INDEX        (FAIL && (card.last_error & (1 << 19)))
#define DATA_TIMEOUT     (FAIL && (card.last_error & (1 << 20)))
#define DATA_CRC         (FAIL && (card.last_error & (1 << 21)))
#define DATA_END_BIT     (FAIL && (card.last_error & (1 << 22)))
#define CURRENT_LIMIT    (FAIL && (card.last_error & (1 << 23)))
#define ACMD12_ERROR     (FAIL && (card.last_error & (1 << 24)))
#define ADMA_ERROR       (FAIL && (card.last_error & (1 << 25)))
#define TUNING_ERROR     (FAIL && (card.last_error & (1 << 26)))

#define SD_VER_UNKNOWN      0
#define SD_VER_1            1
#define SD_VER_1_1          2
#define SD_VER_2            3
#define SD_VER_3            4
#define SD_VER_4            5

const char * const sd_versions[] =
{
    "unknown",
    "1.0 or 1.01",
    "1.10",
    "2.00",
    "3.0x",
    "4.xx"
};

#ifdef EMMC_DEBUG2
const char * const err_irpts[] =
{
    "CMD_TIMEOUT",
    "CMD_CRC",
    "CMD_END_BIT",
    "CMD_INDEX",
    "DATA_TIMEOUT",
    "DATA_CRC",
    "DATA_END_BIT",
    "CURRENT_LIMIT",
    "AUTO_CMD12",
    "ADMA",
    "TUNING",
    "RSVD"
};
#endif

const ULONG sd_commands[] =
{
    SD_CMD_INDEX(0),
    SD_CMD_RESERVED(1),
    SD_CMD_INDEX(2) | SD_RESP_R2,
    SD_CMD_INDEX(3) | SD_RESP_R6,
    SD_CMD_INDEX(4),
    SD_CMD_INDEX(5) | SD_RESP_R4,
    SD_CMD_INDEX(6) | SD_RESP_R1,
    SD_CMD_INDEX(7) | SD_RESP_R1b,
    SD_CMD_INDEX(8) | SD_RESP_R7,
    SD_CMD_INDEX(9) | SD_RESP_R2,
    SD_CMD_INDEX(10) | SD_RESP_R2,
    SD_CMD_INDEX(11) | SD_RESP_R1,
    SD_CMD_INDEX(12) | SD_RESP_R1b | SD_CMD_TYPE_ABORT,
    SD_CMD_INDEX(13) | SD_RESP_R1,
    SD_CMD_RESERVED(14),
    SD_CMD_INDEX(15),
    SD_CMD_INDEX(16) | SD_RESP_R1,
    SD_CMD_INDEX(17) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_INDEX(18) | SD_RESP_R1 | SD_DATA_READ | SD_CMD_MULTI_BLOCK | SD_CMD_BLKCNT_EN | SD_CMD_AUTO_CMD_EN_CMD12,        // SD_CMD_AUTO_CMD_EN_CMD12 not in original driver
    SD_CMD_INDEX(19) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_INDEX(20) | SD_RESP_R1b,
    SD_CMD_RESERVED(21),
    SD_CMD_RESERVED(22),
    SD_CMD_INDEX(23) | SD_RESP_R1,
    SD_CMD_INDEX(24) | SD_RESP_R1 | SD_DATA_WRITE,
    SD_CMD_INDEX(25) | SD_RESP_R1 | SD_DATA_WRITE | SD_CMD_MULTI_BLOCK | SD_CMD_BLKCNT_EN | SD_CMD_AUTO_CMD_EN_CMD12,        // SD_CMD_AUTO_CMD_EN_CMD12 not in original driver
    SD_CMD_RESERVED(26),
    SD_CMD_INDEX(27) | SD_RESP_R1 | SD_DATA_WRITE,
    SD_CMD_INDEX(28) | SD_RESP_R1b,
    SD_CMD_INDEX(29) | SD_RESP_R1b,
    SD_CMD_INDEX(30) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_RESERVED(31),
    SD_CMD_INDEX(32) | SD_RESP_R1,
    SD_CMD_INDEX(33) | SD_RESP_R1,
    SD_CMD_RESERVED(34),
    SD_CMD_RESERVED(35),
    SD_CMD_RESERVED(36),
    SD_CMD_RESERVED(37),
    SD_CMD_INDEX(38) | SD_RESP_R1b,
    SD_CMD_RESERVED(39),
    SD_CMD_RESERVED(40),
    SD_CMD_RESERVED(41),
    SD_CMD_RESERVED(42) | SD_RESP_R1,
    SD_CMD_RESERVED(43),
    SD_CMD_RESERVED(44),
    SD_CMD_RESERVED(45),
    SD_CMD_RESERVED(46),
    SD_CMD_RESERVED(47),
    SD_CMD_RESERVED(48),
    SD_CMD_RESERVED(49),
    SD_CMD_RESERVED(50),
    SD_CMD_RESERVED(51),
    SD_CMD_RESERVED(52),
    SD_CMD_RESERVED(53),
    SD_CMD_RESERVED(54),
    SD_CMD_INDEX(55) | SD_RESP_R1,
    SD_CMD_INDEX(56) | SD_RESP_R1 | SD_CMD_ISDATA,
    SD_CMD_RESERVED(57),
    SD_CMD_RESERVED(58),
    SD_CMD_RESERVED(59),
    SD_CMD_RESERVED(60),
    SD_CMD_RESERVED(61),
    SD_CMD_RESERVED(62),
    SD_CMD_RESERVED(63)
};

const ULONG sd_acommands[] =
{
    SD_CMD_RESERVED(0),
    SD_CMD_RESERVED(1),
    SD_CMD_RESERVED(2),
    SD_CMD_RESERVED(3),
    SD_CMD_RESERVED(4),
    SD_CMD_RESERVED(5),
    SD_CMD_INDEX(6) | SD_RESP_R1,
    SD_CMD_RESERVED(7),
    SD_CMD_RESERVED(8),
    SD_CMD_RESERVED(9),
    SD_CMD_RESERVED(10),
    SD_CMD_RESERVED(11),
    SD_CMD_RESERVED(12),
    SD_CMD_INDEX(13) | SD_RESP_R1,
    SD_CMD_RESERVED(14),
    SD_CMD_RESERVED(15),
    SD_CMD_RESERVED(16),
    SD_CMD_RESERVED(17),
    SD_CMD_RESERVED(18),
    SD_CMD_RESERVED(19),
    SD_CMD_RESERVED(20),
    SD_CMD_RESERVED(21),
    SD_CMD_INDEX(22) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_INDEX(23) | SD_RESP_R1,
    SD_CMD_RESERVED(24),
    SD_CMD_RESERVED(25),
    SD_CMD_RESERVED(26),
    SD_CMD_RESERVED(27),
    SD_CMD_RESERVED(28),
    SD_CMD_RESERVED(29),
    SD_CMD_RESERVED(30),
    SD_CMD_RESERVED(31),
    SD_CMD_RESERVED(32),
    SD_CMD_RESERVED(33),
    SD_CMD_RESERVED(34),
    SD_CMD_RESERVED(35),
    SD_CMD_RESERVED(36),
    SD_CMD_RESERVED(37),
    SD_CMD_RESERVED(38),
    SD_CMD_RESERVED(39),
    SD_CMD_RESERVED(40),
    SD_CMD_INDEX(41) | SD_RESP_R3,
    SD_CMD_INDEX(42) | SD_RESP_R1,
    SD_CMD_RESERVED(43),
    SD_CMD_RESERVED(44),
    SD_CMD_RESERVED(45),
    SD_CMD_RESERVED(46),
    SD_CMD_RESERVED(47),
    SD_CMD_RESERVED(48),
    SD_CMD_RESERVED(49),
    SD_CMD_RESERVED(50),
    SD_CMD_INDEX(51) | SD_RESP_R1 | SD_DATA_READ,
    SD_CMD_RESERVED(52),
    SD_CMD_RESERVED(53),
    SD_CMD_RESERVED(54),
    SD_CMD_RESERVED(55),
    SD_CMD_RESERVED(56),
    SD_CMD_RESERVED(57),
    SD_CMD_RESERVED(58),
    SD_CMD_RESERVED(59),
    SD_CMD_RESERVED(60),
    SD_CMD_RESERVED(61),
    SD_CMD_RESERVED(62),
    SD_CMD_RESERVED(63)
};

// The actual command indices
#define GO_IDLE_STATE           0
#define ALL_SEND_CID            2
#define SEND_RELATIVE_ADDR      3
#define SET_DSR                 4
#define IO_SET_OP_COND          5
#define SWITCH_FUNC             6
#define SELECT_CARD             7
#define DESELECT_CARD           7
#define SELECT_DESELECT_CARD    7
#define SEND_IF_COND            8
#define SEND_CSD                9
#define SEND_CID                10
#define VOLTAGE_SWITCH          11
#define STOP_TRANSMISSION       12
#define SEND_STATUS             13
#define GO_INACTIVE_STATE       15
#define SET_BLOCKLEN            16
#define READ_SINGLE_BLOCK       17
#define READ_MULTIPLE_BLOCK     18
#define SEND_TUNING_BLOCK       19
#define SPEED_CLASS_CONTROL     20
#define SET_BLOCK_COUNT         23
#define WRITE_BLOCK             24
#define WRITE_MULTIPLE_BLOCK    25
#define PROGRAM_CSD             27
#define SET_WRITE_PROT          28
#define CLR_WRITE_PROT          29
#define SEND_WRITE_PROT         30
#define ERASE_WR_BLK_START      32
#define ERASE_WR_BLK_END        33
#define ERASE                   38
#define LOCK_UNLOCK             42
#define APP_CMD                 55
#define GEN_CMD                 56

#define IS_APP_CMD              0x80000000
#define ACMD(a)                 (a | IS_APP_CMD)
#define SET_BUS_WIDTH           (6 | IS_APP_CMD)
#define SD_STATUS               (13 | IS_APP_CMD)
#define SEND_NUM_WR_BLOCKS      (22 | IS_APP_CMD)
#define SET_WR_BLK_ERASE_COUNT  (23 | IS_APP_CMD)
#define SD_SEND_OP_COND         (41 | IS_APP_CMD)
#define SET_CLR_CARD_DETECT     (42 | IS_APP_CMD)
#define SEND_SCR                (51 | IS_APP_CMD)

#define SD_RESET_CMD            (1 << 25)
#define SD_RESET_DAT            (1 << 26)
#define SD_RESET_ALL            (1 << 24)

#define SD_GET_CLOCK_DIVIDER_FAIL    0xffffffff

#define SD_BLOCK_SIZE        512

static ULONG get_base_clock(void);
static ULONG get_clock_divider(ULONG base_clock, ULONG target_rate);
static int switch_clock_rate(ULONG base_clock, ULONG target_rate);
static int card_init(void);
static int card_reset(void);
static int power_on(void);
static void power_off(void);
static int reset_cmd(void);
static int reset_dat(void);
static void handle_interrupts(void);
static void handle_card_interrupt(void);
static BOOL issue_command(ULONG command, ULONG argument, int timeout);
static void issue_command_int(ULONG cmd_reg, ULONG argument, int timeout);
#define DEFAULT_CMD_TIMEOUT 500000
static int ensure_data_mode(void);

static int do_data_command(int is_write, UBYTE *buf, int block_count, ULONG block_no);
static int do_rw(int is_write, UBYTE *buf, int block_count, ULONG block_no);
static int timeout_wait(volatile ULONG* reg, unsigned mask, int value, unsigned usec);

struct TSCR            // SD configuration register
{
    ULONG  scr[2];
    ULONG  sd_bus_widths;
    int    sd_version;
};

struct cardinfo
{
    ULONG hci_ver;

    ULONG device_id[4];

    ULONG card_supports_sdhc;
    ULONG card_supports_18v;
    ULONG card_ocr;
    ULONG card_rca;
    ULONG last_interrupt;
    ULONG last_error;

    struct TSCR scr;

    int failed_voltage_switch;

    ULONG last_cmd_reg;
    ULONG last_cmd;
    ULONG last_cmd_success;
    ULONG last_r0;
    ULONG last_r1;
    ULONG last_r2;
    ULONG last_r3;

    void *buf;
    int blocks_to_transfer;
    size_t block_size;
    int card_removal;
    ULONG base_clock;

    int report_mediach;
};

static struct cardinfo card;

void raspi_act_led_on(void)
{
    // TODO
}

void raspi_act_led_off(void)
{
    // TODO
}


void raspi_emmc_init(void)
{
    peripheral_begin();

    if (card_init() != 0)
    {
        peripheral_end();

        return;
    }

    peripheral_end();
    return;
}

LONG raspi_emmc_rw(WORD rw,LONG sector,WORD count,UBYTE *buf,WORD dev)
{
    if (dev != 0)
        return EUNDEV;

    if (count == 0)
        return 0;

#ifdef EMMC_DEBUG_WP
    if (rw & RW_RW)
    {
        KDEBUG(("raspi_emmc_rw: Protecting against write of count %d to sector %ld\n",count, sector));
        return EWRPRO;
    }
#endif

    raspi_act_led_on();
    peripheral_begin();

    if (do_rw((rw & RW_RW)?1:0, buf, count, sector) != (int) count)
    {
        peripheral_end();
        raspi_act_led_off();

        return (rw&RW_RW) ? EWRITF : EREADF;
    }

    peripheral_end();
    raspi_act_led_off();

    return 0;
}

/*
 *  perform miscellaneous non-data-transfer functions
 */
LONG raspi_emmc_ioctl(UWORD drv,UWORD ctrl,void *arg)
{
    ULONG *info = arg;
    if (drv)
        return EUNDEV;

    ensure_data_mode();
    if (card.card_removal)
    {
        return ctrl == GET_MEDIACHANGE?MEDIACHANGE:E_CHNG;
    }

    switch(ctrl) {
    case GET_DISKINFO:
        info[0] = 31250000;
        info[1] = SECTOR_SIZE;
        return E_OK;
    case GET_DISKNAME:
        sprintf(arg,"%08lx%08lx%08lx%08lx",card.device_id[3], card.device_id[2], card.device_id[1], card.device_id[0]);
        return E_OK;
    case GET_MEDIACHANGE:
        if (card.report_mediach)
        {
            card.report_mediach = 0;
            return MEDIACHANGE;
        }
        else
        {
            return MEDIANOCHANGE;
        }
    default:
        return ERR;
    }
}


static int power_on(void)
{
    prop_tag_2u32_t power_state;

    power_state.value1 = DEVICE_ID_SD_CARD;
    power_state.value2 = POWER_STATE_ON | POWER_STATE_WAIT;
    if (   !raspi_prop_get_tag(PROPTAG_SET_POWER_STATE, &power_state, sizeof power_state, sizeof(ULONG)*2)
        ||  (power_state.value2 & POWER_STATE_NO_DEVICE)
        || !(power_state.value2 & POWER_STATE_ON))
    {
        KINFO(("Device did not power on successfully\n"));

        return -1;
    }

    return 0;
}

static void power_off(void)
{
    // Power off the SD card
    ULONG control0 = EMMC_CONTROL0;
    control0 &= ~(1 << 8);    // Set SD Bus Power bit off in Power Control Register
    EMMC_CONTROL0 = control0;
}

// Get the current base clock rate in Hz
static ULONG get_base_clock(void)
{
    prop_tag_2u32_t tag_clock_rate;
    tag_clock_rate.value1 = CLOCK_ID_EMMC;
    if (!raspi_prop_get_tag(PROPTAG_GET_CLOCK_RATE, &tag_clock_rate, sizeof tag_clock_rate, sizeof(ULONG)*2))
    {
        KINFO(("Cannot get clock rate\n"));

        tag_clock_rate.value2 = 0;
    }

#ifdef EMMC_DEBUG2
    KDEBUG(("Base clock rate is %lu Hz\n", tag_clock_rate.value2));
#endif

    return tag_clock_rate.value2;
}

// Set the clock dividers to generate a target value
static ULONG get_clock_divider(ULONG base_clock, ULONG target_rate)
{
    // TODO: implement use of preset value registers

    ULONG targetted_divisor = 1;
    if (target_rate <= base_clock)
    {
        targetted_divisor = base_clock / target_rate;
        if (base_clock % target_rate)
        {
            targetted_divisor--;
        }
    }

    // Decide on the clock mode to use
    // Currently only 10-bit divided clock mode is supported

#ifndef EMMC_ALLOW_OLD_SDHCI
    if (card.hci_ver >= 2)
    {
#endif
        // HCI version 3 or greater supports 10-bit divided clock mode
        // This requires a power-of-two divider

        // Find the first bit set
        int divisor = -1;
        int first_bit;
        for (first_bit = 31; first_bit >= 0; first_bit--)
        {
            ULONG bit_test = (1 << first_bit);
            if (targetted_divisor & bit_test)
            {
                divisor = first_bit;
                targetted_divisor &= ~bit_test;
                if (targetted_divisor)
                {
                    // The divisor is not a power-of-two, increase it
                    divisor++;
                }

                break;
            }
        }

        if(divisor == -1)
        {
            divisor = 31;
        }
        if(divisor >= 32)
        {
            divisor = 31;
        }

        if(divisor != 0)
        {
            divisor = (1 << (divisor - 1));
        }

        if(divisor >= 0x400)
        {
            divisor = 0x3ff;
        }

        ULONG freq_select = divisor & 0xff;
        ULONG upper_bits = (divisor >> 8) & 0x3;
        ULONG ret = (freq_select << 8) | (upper_bits << 6) | (0 << 5);

#ifdef EMMC_DEBUG2
        int denominator = 1;
        if (divisor != 0)
        {
            denominator = divisor * 2;
        }
        int actual_clock = base_clock / denominator;
        KDEBUG(("base_clock: %ld, target_rate: %ld, divisor: %08x, actual_clock: %d, ret: %08lx\n", base_clock, target_rate, divisor, actual_clock, ret));
#endif

        return ret;
#ifndef EMMC_ALLOW_OLD_SDHCI
    }
    else
    {
        KINFO(("Unsupported host version\n"));

        return SD_GET_CLOCK_DIVIDER_FAIL;
    }
#endif
}

// Switch the clock rate whilst running
static int switch_clock_rate(ULONG base_clock, ULONG target_rate)
{
    // Decide on an appropriate divider
    ULONG divider = get_clock_divider(base_clock, target_rate);
    if (divider == SD_GET_CLOCK_DIVIDER_FAIL)
    {
        KDEBUG(("Couldn't get a valid divider for target rate %ld Hz\n", target_rate));

        return -1;
    }

    // Wait for the command inhibit (CMD and DAT) bits to clear
    while (EMMC_STATUS & 3)
    {
        raspi_delay_us(1000);
    }

    // Set the SD clock off
    ULONG control1 = EMMC_CONTROL1;
    control1 &= ~(1 << 2);
    EMMC_CONTROL1 = control1;
    raspi_delay_us(2000);

    // Write the new divider
    control1 &= ~0xffe0;        // Clear old setting + clock generator select
    control1 |= divider;
    EMMC_CONTROL1 = control1;
    raspi_delay_us(2000);

    // Enable the SD clock
    control1 |= (1 << 2);
    EMMC_CONTROL1 = control1;
    raspi_delay_us(2000);

#ifdef EMMC_DEBUG2
    KDEBUG(("Successfully set clock rate to %ld Hz\n", target_rate));
#endif

    return 0;
}

static int reset_cmd(void)
{
    ULONG control1 = EMMC_CONTROL1;
    control1 |= SD_RESET_CMD;
    EMMC_CONTROL1 = control1;

    if (timeout_wait(&EMMC_CONTROL1, SD_RESET_CMD, 0, 1000000) < 0)
    {
        KINFO(("CMD line did not reset properly\n"));

        return -1;
    }
    return 0;
}

static int reset_dat(void)
{
    ULONG control1 = EMMC_CONTROL1;
    control1 |= SD_RESET_DAT;
    EMMC_CONTROL1 = control1;

    if (timeout_wait(&EMMC_CONTROL1, SD_RESET_DAT, 0, 1000000) < 0)
    {
        KINFO(("DAT line did not reset properly\n"));

        return -1;
    }
    return 0;
}

static void issue_command_int(ULONG cmd_reg, ULONG argument, int timeout)
{
    card.last_cmd_reg = cmd_reg;
    card.last_cmd_success = 0;

    // This is as per HCSS 3.7.1.1/3.7.2.2

#ifdef EMMC_POLL_STATUS_REG
    // Check Command Inhibit
    while (EMMC_STATUS & 1)
    {
        raspi_delay_us(1000);
    }

    // Is the command with busy?
    if ((cmd_reg & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B)
    {
        // With busy

        // Is is an abort command?
        if ((cmd_reg & SD_CMD_TYPE_MASK) != SD_CMD_TYPE_ABORT)
        {
            // Not an abort command

            // Wait for the data line to be free
            while (EMMC_STATUS & 2)
            {
                raspi_delay_us(1000);
            }
        }
    }
#endif

    // Set block size and block count
    // For now, block size = 512 bytes, block count = 1,
    if (card.blocks_to_transfer > 0xffff)
    {
        KDEBUG(("blocks_to_transfer too great (%d)\n", card.blocks_to_transfer));
        card.last_cmd_success = 0;
        return;
    }
    ULONG blksizecnt = card.block_size | (card.blocks_to_transfer << 16);
    EMMC_BLKSIZECNT = blksizecnt;

    // Set argument 1 reg
    EMMC_ARG1 = argument;

    // Set command reg
    EMMC_CMDTM = cmd_reg;

    //raspi_delay_us(2000);

    // Wait for command complete interrupt
    timeout_wait(&EMMC_INTERRUPT, 0x8001, 1, timeout);
    ULONG irpts = EMMC_INTERRUPT;

    // Clear command complete status
    EMMC_INTERRUPT = 0xffff0001;

    // Test for errors
    if ((irpts & 0xffff0001) != 1)
    {
#ifdef EMMC_DEBUG2
        KINFO(("Error occured whilst waiting for command complete interrupt\n"));
#endif
        card.last_error = irpts & 0xffff0000;
        card.last_interrupt = irpts;

        return;
    }

    //raspi_delay_us(2000);

    // Get response data
    switch (cmd_reg & SD_CMD_RSPNS_TYPE_MASK)
    {
    case SD_CMD_RSPNS_TYPE_48:
    case SD_CMD_RSPNS_TYPE_48B:
        card.last_r0 = EMMC_RESP0;
        break;

    case SD_CMD_RSPNS_TYPE_136:
        card.last_r0 = EMMC_RESP0;
        card.last_r1 = EMMC_RESP1;
        card.last_r2 = EMMC_RESP2;
        card.last_r3 = EMMC_RESP3;
        break;
    }

    // If with data, wait for the appropriate interrupt
    if (cmd_reg & SD_CMD_ISDATA)
    {
        ULONG wr_irpt;
        int is_write = 0;
        if(cmd_reg & SD_CMD_DAT_DIR_CH)
        {
            wr_irpt = (1 << 5);     // read
        }
        else
        {
            is_write = 1;
            wr_irpt = (1 << 4);     // write
        }

#ifdef EMMC_DEBUG2
        if (card.blocks_to_transfer > 1)
        {
            KDEBUG(("Multi block transfer\n"));
        }
#endif

        assert (((ULONG) card.buf & 3) == 0);
        ULONG *pData = (ULONG *) card.buf;

        int nBlock;

        for (nBlock = 0; nBlock < card.blocks_to_transfer; nBlock++)
        {
            timeout_wait(&EMMC_INTERRUPT, wr_irpt | 0x8000, 1, timeout);
            irpts = EMMC_INTERRUPT;
            EMMC_INTERRUPT = 0xffff0000 | wr_irpt;

            if ((irpts & (0xffff0000 | wr_irpt)) != wr_irpt)
            {
#ifdef EMMC_DEBUG
                KINFO(("Error occured whilst waiting for data ready interrupt\n"));
#endif
                card.last_error = irpts & 0xffff0000;
                card.last_interrupt = irpts;

                return;
            }

            // Transfer the block
            assert (card.block_size <= 1024);        // internal FIFO size of EMMC
            size_t length = card.block_size;
            assert ((length & 3) == 0);

            if (is_write)
            {
                for (; length > 0; length -= 4)
                {
                    EMMC_DATA = *pData++;
                }
            }
            else
            {
                for (; length > 0; length -= 4)
                {
                    *pData++ = EMMC_DATA;
                }
            }
        }

#ifdef EMMC_DEBUG2
        KDEBUG(("Block transfer complete\n"));
#endif
    }

    // Wait for transfer complete (set if read/write transfer or with busy)
    if (   (cmd_reg & SD_CMD_RSPNS_TYPE_MASK) == SD_CMD_RSPNS_TYPE_48B
        || (cmd_reg & SD_CMD_ISDATA))
    {
#ifdef EMMC_POLL_STATUS_REG
        // First check command inhibit (DAT) is not already 0
        if ((EMMC_STATUS & 2) == 0)
        {
            EMMC_INTERRUPT = 0xffff0002;
        }
        else
#endif
        {
            timeout_wait(&EMMC_INTERRUPT, 0x8002, 1, timeout);
            irpts = EMMC_INTERRUPT;
            EMMC_INTERRUPT = 0xffff0002;

            // Handle the case where both data timeout and transfer complete
            //  are set - transfer complete overrides data timeout: HCSS 2.2.17
            if (   ((irpts & 0xffff0002) != 2)
                && ((irpts & 0xffff0002) != 0x100002))
            {
#ifdef EMMC_DEBUG
                KINFO(("Error occured whilst waiting for transfer complete interrupt\n"));
#endif
                card.last_error = irpts & 0xffff0000;
                card.last_interrupt = irpts;

                return;
            }

            EMMC_INTERRUPT = 0xffff0002;
        }
    }

    // Return success
    card.last_cmd_success = 1;
}

static void handle_card_interrupt(void)
{
    // Handle a card interrupt

#ifdef EMMC_DEBUG2
    ULONG status = EMMC_STATUS;

    KDEBUG(("Card interrupt\n"));
    KDEBUG(("controller status: %08lx\n", status));
#endif

    // Get the card status
    if(card.card_rca)
    {
        issue_command_int(sd_commands[SEND_STATUS], card.card_rca << 16, 500000);
        if (FAIL)
        {
#ifdef EMMC_DEBUG
            KINFO(("Unable to get card status\n"));
#endif
        }
        else
        {
#ifdef EMMC_DEBUG2
            KDEBUG(("card status: %08lx\n", card.last_r0));
#endif
        }
    }
    else
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("no card currently selected\n"));
#endif
    }
}

static void handle_interrupts(void)
{
    ULONG irpts = EMMC_INTERRUPT;
    ULONG reset_mask = 0;

    if (irpts & SD_COMMAND_COMPLETE)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious command complete interrupt\n"));
#endif
        reset_mask |= SD_COMMAND_COMPLETE;
    }

    if (irpts & SD_TRANSFER_COMPLETE)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious transfer complete interrupt\n"));
#endif
        reset_mask |= SD_TRANSFER_COMPLETE;
    }

    if (irpts & SD_BLOCK_GAP_EVENT)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious block gap event interrupt\n"));
#endif
        reset_mask |= SD_BLOCK_GAP_EVENT;
    }

    if (irpts & SD_DMA_INTERRUPT)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious DMA interrupt\n"));
#endif
        reset_mask |= SD_DMA_INTERRUPT;
    }

    if (irpts & SD_BUFFER_WRITE_READY)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious buffer write ready interrupt\n"));
#endif
        reset_mask |= SD_BUFFER_WRITE_READY;
        reset_dat();
    }

    if (irpts & SD_BUFFER_READ_READY)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious buffer read ready interrupt\n"));
#endif
        reset_mask |= SD_BUFFER_READ_READY;
        reset_dat();
    }

    if (irpts & SD_CARD_INSERTION)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("card insertion detected\n"));
#endif
        reset_mask |= SD_CARD_INSERTION;
    }

    if (irpts & SD_CARD_REMOVAL)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("card removal detected\n"));
#endif
        reset_mask |= SD_CARD_REMOVAL;
        card.card_removal = 1;
    }

    if (irpts & SD_CARD_INTERRUPT)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("card interrupt detected\n"));
#endif
        handle_card_interrupt();
        reset_mask |= SD_CARD_INTERRUPT;
    }

    if (irpts & 0x8000)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("spurious error interrupt: %08lx\n", irpts));
#endif
        reset_mask |= 0xffff0000;
    }

    EMMC_INTERRUPT = reset_mask;
}

static BOOL issue_command(ULONG command, ULONG argument, int timeout)
{
    // First, handle any pending interrupts
    handle_interrupts();

    // Stop the command issue if it was the card remove interrupt that was handled
    if (card.card_removal)
    {
        card.last_cmd_success = 0;
        return FALSE;
    }

    // Now run the appropriate commands by calling issue_command_int()
    if (command & IS_APP_CMD)
    {
        command &= 0xff;
#ifdef EMMC_DEBUG2
        KDEBUG(("Issuing command ACMD%lu\n", command));
#endif

        if (sd_acommands[command] == SD_CMD_RESERVED(0))
        {
            KINFO(("Invalid command ACMD%lu\n", command));
            card.last_cmd_success = 0;

            return FALSE;
        }
        card.last_cmd = APP_CMD;

        ULONG rca = 0;
        if(card.card_rca)
        {
            rca = card.card_rca << 16;
        }
        issue_command_int(sd_commands[APP_CMD], rca, timeout);
        if (card.last_cmd_success)
        {
            card.last_cmd = command | IS_APP_CMD;
            issue_command_int(sd_acommands[command], argument, timeout);
        }
    }
    else
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("Issuing command CMD%lu\n", command));
#endif

        if (sd_commands[command] == SD_CMD_RESERVED(0))
        {
            KINFO(("Invalid command CMD%lu\n", command));
            card.last_cmd_success = 0;

            return FALSE;
        }

        card.last_cmd = command;
        issue_command_int(sd_commands[command], argument, timeout);
    }

#ifdef EMMC_DEBUG2
    if (FAIL)
    {
        KINFO(("Error issuing %s%lu (intr %08lx)\n", card.last_cmd & IS_APP_CMD ? "ACMD" : "CMD", card.last_cmd & 0xff, card.last_interrupt));

        if (card.last_error == 0)
        {
            KDEBUG(("TIMEOUT\n"));
        }
        else
        {
            int i;
            for (i = 0; i < SD_ERR_RSVD; i++)
            {
                if (card.last_error & (1 << (i + 16)))
                {
                    KDEBUG((err_irpts[i]));
                }
            }
        }
    }
    else
    {
        KDEBUG(("command completed successfully\n"));
    }
#endif

    return card.last_cmd_success;
}

static int card_reset(void)
{
#ifdef EMMC_DEBUG2
    KDEBUG(("Resetting controller\n"));
#endif

    ULONG control1 = EMMC_CONTROL1;
    control1 |= (1 << 24);
    // Disable clock
    control1 &= ~(1 << 2);
    control1 &= ~(1 << 0);
    EMMC_CONTROL1 = control1;
    if (timeout_wait(&EMMC_CONTROL1, 7 << 24, 0, 1000000) < 0)
    {
        KINFO(("Controller did not reset properly\n"));

        return -1;
    }
#ifdef EMMC_DEBUG2
    KDEBUG(("control0: %08lx, control1: %08lx, control2: %08lx\n",
                EMMC_CONTROL0, EMMC_CONTROL1, EMMC_CONTROL2));
#endif

    // Check for a valid card
#ifdef EMMC_DEBUG2
    KDEBUG(("checking for an inserted card\n"));
#endif
    timeout_wait(&EMMC_STATUS, 1 << 16, 1, 500000);
    ULONG status_reg = EMMC_STATUS;
    if ((status_reg & (1 << 16)) == 0)
    {
        KINFO(("no card inserted\n"));

        return -1;
    }
#ifdef EMMC_DEBUG2
    KDEBUG(("status: %08lx\n", status_reg));
#endif

    // Clear control2
    EMMC_CONTROL2 = 0;

    // Get the base clock rate
    ULONG base_clock = get_base_clock();
    if (base_clock == 0)
    {
        KINFO(("assuming clock rate to be 100MHz\n"));
        base_clock = 100000000;
    }

    // Set clock rate to something slow
#ifdef EMMC_DEBUG2
    KDEBUG(("setting clock rate\n"));
#endif
    control1 = EMMC_CONTROL1;
    control1 |= 1;            // enable clock

    // Set to identification frequency (400 kHz)
    ULONG f_id = get_clock_divider(base_clock, SD_CLOCK_ID);
    if (f_id == SD_GET_CLOCK_DIVIDER_FAIL)
    {
        KDEBUG(("unable to get a valid clock divider for ID frequency\n"));

        return -1;
    }
    control1 |= f_id;

    // was not masked out and or'd with (7 << 16) in original driver
    control1 &= ~(0xF << 16);
    control1 |= (11 << 16);        // data timeout = TMCLK * 2^24

    EMMC_CONTROL1 = control1;

    if (timeout_wait(&EMMC_CONTROL1, 2, 1, 1000000) < 0)
    {
        KINFO(("Clock did not stabilise within 1 second\n"));

        return -1;
    }
#ifdef EMMC_DEBUG2
    KDEBUG(("control0: %08lx, control1: %08lx\n",
                EMMC_CONTROL0, EMMC_CONTROL1));
#endif

    // Enable the SD clock
#ifdef EMMC_DEBUG2
    KDEBUG(("enabling SD clock\n"));
#endif
    raspi_delay_us(2000);
    control1 = EMMC_CONTROL1;
    control1 |= 4;
    EMMC_CONTROL1 = control1;
    raspi_delay_us(2000);

    // Mask off sending interrupts to the ARM
    EMMC_IRPT_EN = 0;
    // Reset interrupts
    EMMC_INTERRUPT = 0xffffffff;
    // Have all interrupts sent to the INTERRUPT register
    ULONG irpt_mask = 0xffffffff & (~SD_CARD_INTERRUPT);
#ifdef SD_CARD_INTERRUPTS
    irpt_mask |= SD_CARD_INTERRUPT;
#endif
    EMMC_IRPT_MASK = irpt_mask;

    raspi_delay_us(2000);

    // >> Prepare the device structure
    card.device_id[0] = 0;
    card.device_id[1] = 0;
    card.device_id[2] = 0;
    card.device_id[3] = 0;

    card.card_supports_sdhc = 0;
    card.card_supports_18v = 0;
    card.card_ocr = 0;
    card.card_rca = 0;
    card.last_interrupt = 0;
    card.last_error = 0;

    card.failed_voltage_switch = 0;

    card.last_cmd_reg = 0;
    card.last_cmd = 0;
    card.last_cmd_success = 0;
    card.last_r0 = 0;
    card.last_r1 = 0;
    card.last_r2 = 0;
    card.last_r3 = 0;

    card.buf = 0;
    card.blocks_to_transfer = 0;
    card.block_size = 0;
    card.card_removal = 0;
    card.base_clock = 0;
    // << Prepare the device structure

    card.base_clock = base_clock;

    // Send CMD0 to the card (reset to idle state)
    if (!issue_command(GO_IDLE_STATE, 0, DEFAULT_CMD_TIMEOUT))
    {
        KINFO(("no CMD0 response\n"));

        return -1;
    }

    // Send CMD8 to the card
    // Voltage supplied = 0x1 = 2.7-3.6V (standard)
    // Check pattern = 10101010b (as per PLSS 4.3.13) = 0xAA
#ifdef EMMC_DEBUG2
    KDEBUG(("Note a timeout error on the following command (CMD8) is normal and expected if the SD card version is less than 2.0\n"));
#endif
    issue_command(SEND_IF_COND, 0x1aa, DEFAULT_CMD_TIMEOUT);
    int v2_later = 0;
    if (TIMEOUT)
    {
        v2_later = 0;
    }
    else if (CMD_TIMEOUT)
    {
        if(reset_cmd() == -1)
        {
            return -1;
        }
        EMMC_INTERRUPT = SD_ERR_MASK_CMD_TIMEOUT;
        v2_later = 0;
    }
    else if (FAIL)
    {
        KINFO(("failure sending CMD8 (%08lx)\n", card.last_interrupt));

        return -1;
    }
    else
    {
        if ((card.last_r0 & 0xfff) != 0x1aa)
        {
            KINFO(("unusable card\n"));
#ifdef EMMC_DEBUG
            KDEBUG(("CMD8 response %08lx\n", card.last_r0));
#endif

            return -1;
        }
        else
        {
            v2_later = 1;
        }
    }

    // Here we are supposed to check the response to CMD5 (HCSS 3.6)
    // It only returns if the card is a SDIO card
#ifdef EMMC_DEBUG2
    KDEBUG(("Note that a timeout error on the following command (CMD5) is normal and expected if the card is not a SDIO card.\n"));
#endif
    issue_command(IO_SET_OP_COND, 0, 10000);
    if (!TIMEOUT)
    {
        if (CMD_TIMEOUT)
        {
            if (reset_cmd() == -1)
            {
                return -1;
            }

            EMMC_INTERRUPT = SD_ERR_MASK_CMD_TIMEOUT;
        }
        else
        {
            KINFO(("SDIO card detected - not currently supported\n"));
#ifdef EMMC_DEBUG2
            KDEBUG(("CMD5 returned %08lx\n", card.last_r0));
#endif

            return -1;
        }
    }

    // Call an inquiry ACMD41 (voltage window = 0) to get the OCR
#ifdef EMMC_DEBUG2
    KDEBUG(("sending inquiry ACMD41\n"));
#endif
    if (!issue_command(ACMD(41), 0, DEFAULT_CMD_TIMEOUT))
    {
        KINFO(("Inquiry ACMD41 failed\n"));
        return -1;
    }
#ifdef EMMC_DEBUG2
    KDEBUG(("inquiry ACMD41 returned %08lx\n", card.last_r0));
#endif

    // Call initialization ACMD41
    int card_is_busy = 1;
    while (card_is_busy)
    {
        ULONG v2_flags = 0;
        if (v2_later)
        {
            // Set SDHC support
            v2_flags |= (1 << 30);

            // Set 1.8v support
#ifdef SD_1_8V_SUPPORT
            if (!card.failed_voltage_switch)
            {
                v2_flags |= (1 << 24);
            }
#endif
#ifdef SDXC_MAXIMUM_PERFORMANCE
            // Enable SDXC maximum performance
            v2_flags |= (1 << 28);
#endif
        }

        if (!issue_command(ACMD(41), 0x00ff8000 | v2_flags, DEFAULT_CMD_TIMEOUT))
        {
            KINFO(("Error issuing ACMD41\n"));

            return -1;
        }

        if ((card.last_r0 >> 31) & 1)
        {
            // Initialization is complete
            card.card_ocr = (card.last_r0 >> 8) & 0xffff;
            card.card_supports_sdhc = (card.last_r0 >> 30) & 0x1;
#ifdef SD_1_8V_SUPPORT
            if (!card.failed_voltage_switch)
            {
                card.card_supports_18v = (card.last_r0 >> 24) & 0x1;
            }
#endif

            card_is_busy = 0;
        }
        else
        {
            // Card is still busy
#ifdef EMMC_DEBUG2
            KDEBUG(("Card is busy, retrying\n"));
#endif
            raspi_delay_us(500000);
        }
    }

#ifdef EMMC_DEBUG2
    KDEBUG(("card identified: OCR: %04lx, 1.8v support: %ld, SDHC support: %ld\n", card.card_ocr, card.card_supports_18v, card.card_supports_sdhc));
#endif

    // At this point, we know the card is definitely an SD card, so will definitely
    //  support SDR12 mode which runs at 25 MHz
    switch_clock_rate(base_clock, SD_CLOCK_NORMAL);

    // A small wait before the voltage switch
    raspi_delay_us(5000);

    // Switch to 1.8V mode if possible
    if (card.card_supports_18v)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("switching to 1.8V mode\n"));
#endif
        // As per HCSS 3.6.1

        // Send VOLTAGE_SWITCH
        if (!issue_command(VOLTAGE_SWITCH, 0, DEFAULT_CMD_TIMEOUT))
        {
#ifdef EMMC_DEBUG
            KDEBUG(("error issuing VOLTAGE_SWITCH\n"));
#endif
            card.failed_voltage_switch = 1;
            power_off();

            return card_reset();
        }

        // Disable SD clock
        control1 = EMMC_CONTROL1;
        control1 &= ~(1 << 2);
        EMMC_CONTROL1 = control1;

        // Check DAT[3:0]
        status_reg = EMMC_STATUS;
        ULONG dat30 = (status_reg >> 20) & 0xf;
        if (dat30 != 0)
        {
#ifdef EMMC_DEBUG
            KDEBUG(("DAT[3:0] did not settle to 0\n"));
#endif
            card.failed_voltage_switch = 1;
            power_off();

            return card_reset();
        }

        // Set 1.8V signal enable to 1
        ULONG control0 = EMMC_CONTROL0;
        control0 |= (1 << 8);
        EMMC_CONTROL0 = control0;

        // Wait 5 ms
        raspi_delay_us(5000);

        // Check the 1.8V signal enable is set
        control0 = EMMC_CONTROL0;
        if(((control0 >> 8) & 1) == 0)
        {
#ifdef EMMC_DEBUG
            KDEBUG(("controller did not keep 1.8V signal enable high\n"));
#endif
            card.failed_voltage_switch = 1;
            power_off();

            return card_reset();
        }

        // Re-enable the SD clock
        control1 = EMMC_CONTROL1;
        control1 |= (1 << 2);
        EMMC_CONTROL1 = control1;

        raspi_delay_us(10000);

        // Check DAT[3:0]
        status_reg = EMMC_STATUS;
        dat30 = (status_reg >> 20) & 0xf;
        if (dat30 != 0xf)
        {
#ifdef EMMC_DEBUG
            KDEBUG(("DAT[3:0] did not settle to 1111b (%01lx)\n", dat30));
#endif
            card.failed_voltage_switch = 1;
            power_off();

            return card_reset();
        }

#ifdef EMMC_DEBUG2
        KDEBUG(("voltage switch complete\n"));
#endif
    }

    // Send CMD2 to get the cards CID
    if (!issue_command(ALL_SEND_CID, 0, DEFAULT_CMD_TIMEOUT))
    {
        KINFO(("error sending ALL_SEND_CID\n"));

        return -1;
    }
    card.device_id[0] = card.last_r0;
    card.device_id[1] = card.last_r1;
    card.device_id[2] = card.last_r2;
    card.device_id[3] = card.last_r3;
#ifdef EMMC_DEBUG2
    KDEBUG(("Card CID: %08lx%08lx%08lx%08lx\n",
                card.device_id[3], card.device_id[2], card.device_id[1], card.device_id[0]));
#endif

    // Send CMD3 to enter the data state
    if (!issue_command(SEND_RELATIVE_ADDR, 0, DEFAULT_CMD_TIMEOUT))
    {
        KINFO(("error sending SEND_RELATIVE_ADDR\n"));

        return -1;
    }

    ULONG cmd3_resp = card.last_r0;
#ifdef EMMC_DEBUG2
    KDEBUG(("CMD3 response: %08lx\n", cmd3_resp));
#endif

    card.card_rca = (cmd3_resp >> 16) & 0xffff;
    ULONG crc_error = (cmd3_resp >> 15) & 0x1;
    ULONG illegal_cmd = (cmd3_resp >> 14) & 0x1;
    ULONG error = (cmd3_resp >> 13) & 0x1;
    ULONG status = (cmd3_resp >> 9) & 0xf;
    ULONG ready = (cmd3_resp >> 8) & 0x1;

    if (crc_error)
    {
        KINFO(("CRC error\n"));

        return -1;
    }

    if (illegal_cmd)
    {
        KINFO(("illegal command\n"));

        return -1;
    }

    if (error)
    {
        KINFO(("generic error\n"));

        return -1;
    }

    if (!ready)
    {
        KINFO(("not ready for data\n"));

        return -1;
    }

#ifdef EMMC_DEBUG2
    KDEBUG(("RCA: %04lx\n", card.card_rca));
#endif

    // Now select the card (toggles it to transfer state)
    if (!issue_command(SELECT_CARD, card.card_rca << 16, DEFAULT_CMD_TIMEOUT))
    {
        KDEBUG(("error sending CMD7\n"));

        return -1;
    }

    ULONG cmd7_resp = card.last_r0;
    status = (cmd7_resp >> 9) & 0xf;

    if((status != 3) && (status != 4))
    {
        KINFO(("Invalid status (%lu)\n", status));

        return -1;
    }

    // If not an SDHC card, ensure BLOCKLEN is 512 bytes
    if(!card.card_supports_sdhc)
    {
        if (!issue_command(SET_BLOCKLEN, SD_BLOCK_SIZE, DEFAULT_CMD_TIMEOUT))
        {
            KINFO(("Error sending SET_BLOCKLEN\n"));

            return -1;
        }
    }
    ULONG controller_block_size = EMMC_BLKSIZECNT;
    controller_block_size &= (~0xfff);
    controller_block_size |= 0x200;
    EMMC_BLKSIZECNT = controller_block_size;

    // Get the cards SCR register
    card.buf = &card.scr.scr[0];
    card.block_size = 8;
    card.blocks_to_transfer = 1;
    issue_command(SEND_SCR, 0, DEFAULT_CMD_TIMEOUT);
    card.block_size = SD_BLOCK_SIZE;
    if (FAIL)
    {
        KINFO(("Error sending SEND_SCR\n"));

        return -1;
    }

    // Determine card version
    // Note that the SCR is big-endian
    ULONG scr0 = be2cpu32 (card.scr.scr[0]);
    card.scr.sd_version = SD_VER_UNKNOWN;
    ULONG sd_spec = (scr0 >> (56 - 32)) & 0xf;
    ULONG sd_spec3 = (scr0 >> (47 - 32)) & 0x1;
    ULONG sd_spec4 = (scr0 >> (42 - 32)) & 0x1;
    card.scr.sd_bus_widths = (scr0 >> (48 - 32)) & 0xf;
    if (sd_spec == 0)
    {
        card.scr.sd_version = SD_VER_1;
    }
    else if (sd_spec == 1)
    {
        card.scr.sd_version = SD_VER_1_1;
    }
    else if (sd_spec == 2)
    {
        if (sd_spec3 == 0)
        {
            card.scr.sd_version = SD_VER_2;
        }
        else if (sd_spec3 == 1)
        {
            if (sd_spec4 == 0)
            {
                card.scr.sd_version = SD_VER_3;
            }
            else if (sd_spec4 == 1)
            {
                card.scr.sd_version = SD_VER_4;
            }
        }
    }
#ifdef EMMC_DEBUG2
    KDEBUG(("SCR[0]: %08lx, SCR[1]: %08lx\n", card.scr.scr[0], card.scr.scr[1]));;
    KDEBUG(("SCR: %08lx%08lx\n", be2cpu32(card.scr.scr[0]), be2cpu32(card.scr.scr[1])));
    KDEBUG(("SCR: version %s, bus_widths %01lx\n", sd_versions[card.scr.sd_version], card.scr.sd_bus_widths));
#endif

    if (card.scr.sd_bus_widths & 4)
    {
        // Set 4-bit transfer mode (ACMD6)
        // See HCSS 3.4 for the algorithm
#ifdef SD_4BIT_DATA
#ifdef EMMC_DEBUG2
        KDEBUG(("Switching to 4-bit data mode\n"));
#endif

        // Disable card interrupt in host
        ULONG old_irpt_mask = EMMC_IRPT_MASK;
        ULONG new_iprt_mask = old_irpt_mask & ~(1 << 8);
        EMMC_IRPT_MASK = new_iprt_mask;

        // Send ACMD6 to change the card's bit mode
        if (!issue_command(SET_BUS_WIDTH, 2, DEFAULT_CMD_TIMEOUT))
        {
            KINFO(("Switch to 4-bit data mode failed\n"));
        }
        else
        {
            // Change bit mode for Host
            ULONG control0 = EMMC_CONTROL0;
            control0 |= 0x2;
            EMMC_CONTROL0 = control0;

            // Re-enable card interrupt in host
            EMMC_IRPT_MASK = old_irpt_mask;

#ifdef EMMC_DEBUG2
            KDEBUG(("switch to 4-bit complete\n"));
#endif
        }
#endif
    }

    KINFO(("Found a valid version %s SD card\n", sd_versions[card.scr.sd_version]));
#ifdef EMMC_DEBUG2
    KDEBUG(("setup successful (status %ld)\n", status));
#endif

    // Reset interrupt register
    EMMC_INTERRUPT = 0xffffffff;

    return 0;
}

static int card_init(void)
{
    if(power_on() != 0)
    {
        KINFO(("BCM2708 controller did not power on successfully\n"));

        return -1;
    }

    // Check the sanity of the sd_commands and sd_acommands structures
    assert (sizeof (sd_commands) == (64 * sizeof(ULONG)));
    assert (sizeof (sd_acommands) == (64 * sizeof(ULONG)));

    // Read the controller version
    ULONG ver = EMMC_SLOTISR_VER;
    ULONG sdversion = (ver >> 16) & 0xff;
#ifdef EMMC_DEBUG2
    ULONG vendor = ver >> 24;
    ULONG slot_status = ver & 0xff;
    KDEBUG(("Vendor %lx, SD version %lx, slot status %lx\n", vendor, sdversion, slot_status));
#endif
    card.hci_ver = sdversion;
    if (card.hci_ver < 2)
    {
#ifdef EMMC_ALLOW_OLD_SDHCI
        KINFO(("Old SDHCI version detected\n"));
#else
        KINFO(("Only SDHCI versions >= 3.0 are supported\n"));

        return -1;
#endif
    }

    if (card_reset() != 0)
    {
        return -1;
    }

    return 0;
}

static int ensure_data_mode(void)
{
    if (card.card_rca == 0)
    {
        // Try again to initialise the card
        int ret = card_reset();
        if (ret != 0)
        {
            return ret;
        }
    }

#ifdef EMMC_DEBUG2
    KDEBUG(("ensure_data_mode() obtaining status register for card_rca %08lx: \n", card.card_rca));
#endif

    if (!issue_command(SEND_STATUS, card.card_rca << 16, DEFAULT_CMD_TIMEOUT))
    {
        KINFO(("ensure_data_mode() error sending CMD13\n"));
        card.card_rca = 0;

        return -1;
    }

    ULONG status = card.last_r0;
    ULONG cur_state = (status >> 9) & 0xf;
#ifdef EMMC_DEBUG2
    KDEBUG(("status %ld\n", cur_state));
#endif
    if (cur_state == 3)
    {
        // Currently in the stand-by state - select it
        if (!issue_command(SELECT_CARD, card.card_rca << 16, DEFAULT_CMD_TIMEOUT))
        {
            KINFO(("ensure_data_mode() no response from CMD17\n"));
            card.card_rca = 0;

            return -1;
        }
    }
    else if (cur_state == 5)
    {
        // In the data transfer state - cancel the transmission
        if(!issue_command(STOP_TRANSMISSION, 0, DEFAULT_CMD_TIMEOUT))
        {
            KINFO(("ensure_data_mode() no response from CMD12\n"));
            card.card_rca = 0;

            return -1;
        }

        // Reset the data circuit
        reset_dat();
    }
    else if (cur_state != 4)
    {
        // Not in the transfer state - re-initialise
        int ret = card_reset();
        if (ret != 0)
        {
            return ret;
        }
    }

    // Check again that we're now in the correct mode
    if (cur_state != 4)
    {
#ifdef EMMC_DEBUG2
        KDEBUG(("ensure_data_mode() rechecking status: \n"));
#endif
        if (!issue_command(SEND_STATUS, card.card_rca << 16, DEFAULT_CMD_TIMEOUT))
        {
            KINFO(("ensure_data_mode() no response from CMD13\n"));
            card.card_rca = 0;

            return -1;
        }
        status = card.last_r0;
        cur_state = (status >> 9) & 0xf;
#ifdef EMMC_DEBUG2
        KDEBUG(("status %ld\n", cur_state));
#endif

        if(cur_state != 4)
        {
            KINFO(("unable to initialise SD card to data mode (state %lu)\n", cur_state));
            card.card_rca = 0;

            return -1;
        }
    }

    return 0;
}

static int do_data_command(int is_write, UBYTE *buf, int block_count, ULONG block_no)
{
    // PLSS table 4.20 - SDSC cards use byte addresses rather than block addresses
    if (!card.card_supports_sdhc)
    {
        block_no *= SD_BLOCK_SIZE;
    }

    // This is as per HCSS 3.7.2.1
    if(block_count < 1)
    {
        KINFO(("do_data_command() called with block count (%d) less than 1\n", block_count));

        return -1;
    }

    card.blocks_to_transfer = block_count;
    card.buf = buf;

    // Decide on the command to use
    int command;
    if (is_write)
    {
        if(card.blocks_to_transfer > 1)
        {
            command = WRITE_MULTIPLE_BLOCK;
        }
        else
        {
            command = WRITE_BLOCK;
        }
    }
    else
    {
        if(card.blocks_to_transfer > 1)
        {
            command = READ_MULTIPLE_BLOCK;
        }
        else
        {
            command = READ_SINGLE_BLOCK;
        }
    }

    int retry_count = 0;
    int max_retries = 3;
    while (retry_count < max_retries)
    {
        if (issue_command(command, block_no, 5000000))
        {
            break;
        }
        else
        {
            KINFO(("error sending CMD%d\n", command));
            KDEBUG(("error = %08lx\n", card.last_error));

            if (++retry_count < max_retries)
            {
                KDEBUG(("Retrying\n"));
            }
            else
            {
                KDEBUG(("Giving up\n"));
            }
        }
    }

    if (retry_count == max_retries)
    {
        card.card_rca = 0;

        return -1;
    }

    return 0;
}

static int do_rw(int is_write, UBYTE *buf, int block_count, ULONG block_no)
{
    // Check the status of the card
    if (ensure_data_mode() != 0)
    {
        return -1;
    }

#ifdef EMMC_DEBUG2
    KDEBUG(((is_write?"Writing to block %lu\n":"Reading from block %lu\n"), block_no));
#endif

    if (do_data_command(is_write, buf, block_count, block_no) < 0)
    {
        return -1;
    }

#ifdef EMMC_DEBUG2
    KDEBUG((is_write?"Data write successful\n":"Data read successful\n"));
#endif

    return block_count;
}


static int timeout_wait(volatile ULONG* reg, unsigned mask, int value, unsigned usec)
{
    unsigned nCount = usec / 1000;

    do
    {
        raspi_delay_us(1);

        if ((*reg & mask) ? value : !value)
        {
            return 0;
        }

        raspi_delay_us(999);
    }
    while (nCount--);

    return -1;
}

#endif
