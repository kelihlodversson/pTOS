/*
 * pci.h - native pTOS PCI access layer
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PCI_H
#define PCI_H

#include "portab.h"

typedef ULONG PCI_HANDLE;

#define PCI_HANDLE_NONE              0UL

#define PCI_SUCCESSFUL               0L
#define PCI_FUNC_NOT_SUPPORTED      -2L
#define PCI_BAD_VENDOR_ID           -3L
#define PCI_DEVICE_NOT_FOUND        -4L
#define PCI_BAD_REGISTER_NUMBER     -5L
#define PCI_SET_FAILED              -6L
#define PCI_BUFFER_TOO_SMALL        -7L
#define PCI_GENERAL_ERROR           -8L
#define PCI_BAD_HANDLE              -9L
#define PCI_BAD_RESOURCE           -10L

#define PCI_ANY_VENDOR          0xffffU

#define PCI_CLASS_MASK_PROGIF   0x01000000UL
#define PCI_CLASS_MASK_SUBCLASS 0x02000000UL
#define PCI_CLASS_MASK_BASE     0x04000000UL
#define PCI_CLASS_CODE_MASK     0x00ffffffUL

#define PCI_RESOURCE_IO         0x4000U
#define PCI_RESOURCE_LAST       0x8000U
#define PCI_RESOURCE_8BIT       0x0100U
#define PCI_RESOURCE_16BIT      0x0200U
#define PCI_RESOURCE_32BIT      0x0400U
#define PCI_RESOURCE_ORDER_MOTOROLA 0x0000U
#define PCI_RESOURCE_ORDER_INTEL    0x000fU

#define PCI_CONFIG_VENDOR_ID    0x00U
#define PCI_CONFIG_DEVICE_ID    0x02U
#define PCI_CONFIG_COMMAND      0x04U
#define PCI_CONFIG_STATUS       0x06U
#define PCI_CONFIG_PROGIF       0x09U
#define PCI_CONFIG_SUBCLASS     0x0aU
#define PCI_CONFIG_BASE_CLASS   0x0bU
#define PCI_CONFIG_HEADER_TYPE  0x0eU
#define PCI_CONFIG_PRIMARY_BUS   0x18U
#define PCI_CONFIG_SECONDARY_BUS 0x19U
#define PCI_CONFIG_SUBORDINATE_BUS 0x1aU
#define PCI_CONFIG_BAR0         0x10U
#define PCI_CONFIG_INTERRUPT_LINE 0x3cU
#define PCI_CONFIG_INTERRUPT_PIN  0x3dU

#define PCI_MAX_BARS            6

typedef struct {
    UWORD next;
    UWORD flags;
    ULONG start;
    ULONG length;
    ULONG offset;
    ULONG dmaoffset;
} pci_resource_t;

typedef struct {
    ULONG address;
    ULONG length;
} pci_mem_t;

typedef void (*pci_interrupt_handler_t)(void *param);
typedef LONG (*pci_card_callback_t)(LONG function);

LONG pci_init(void);
LONG pci_find_device(UWORD vendor, UWORD device, UWORD index, PCI_HANDLE *handle);
LONG pci_find_classcode(ULONG classcode, ULONG mask, UWORD index, PCI_HANDLE *handle);
LONG pci_read_config_byte(PCI_HANDLE handle, UWORD reg, UBYTE *value);
LONG pci_read_config_word(PCI_HANDLE handle, UWORD reg, UWORD *value);
LONG pci_read_config_long(PCI_HANDLE handle, UWORD reg, ULONG *value);
LONG pci_write_config_byte(PCI_HANDLE handle, UWORD reg, UBYTE value);
LONG pci_write_config_word(PCI_HANDLE handle, UWORD reg, UWORD value);
LONG pci_write_config_long(PCI_HANDLE handle, UWORD reg, ULONG value);
LONG pci_get_resource(PCI_HANDLE handle, UWORD bar, pci_resource_t *resource);
LONG pci_read_mem_byte(PCI_HANDLE handle, ULONG address, UBYTE *value);
LONG pci_read_mem_word(PCI_HANDLE handle, ULONG address, UWORD *value);
LONG pci_read_mem_long(PCI_HANDLE handle, ULONG address, ULONG *value);
LONG pci_write_mem_byte(PCI_HANDLE handle, ULONG address, UBYTE value);
LONG pci_write_mem_word(PCI_HANDLE handle, ULONG address, UWORD value);
LONG pci_write_mem_long(PCI_HANDLE handle, ULONG address, ULONG value);
LONG pci_read_io_byte(PCI_HANDLE handle, ULONG address, UBYTE *value);
LONG pci_read_io_word(PCI_HANDLE handle, ULONG address, UWORD *value);
LONG pci_read_io_long(PCI_HANDLE handle, ULONG address, ULONG *value);
LONG pci_write_io_byte(PCI_HANDLE handle, ULONG address, UBYTE value);
LONG pci_write_io_word(PCI_HANDLE handle, ULONG address, UWORD value);
LONG pci_write_io_long(PCI_HANDLE handle, ULONG address, ULONG value);
LONG pci_hook_interrupt(PCI_HANDLE handle, pci_interrupt_handler_t handler, void *param);
LONG pci_unhook_interrupt(PCI_HANDLE handle);
LONG pci_get_card_used(PCI_HANDLE handle, pci_card_callback_t *callback);
LONG pci_set_card_used(PCI_HANDLE handle, pci_card_callback_t callback, LONG state);
LONG pci_get_pagesize(ULONG *pagesize);
LONG pci_virt_to_bus(PCI_HANDLE handle, ULONG address, pci_mem_t *mem);
LONG pci_bus_to_virt(PCI_HANDLE handle, ULONG address, pci_mem_t *mem);
LONG pci_virt_to_phys(ULONG address, pci_mem_t *mem);
LONG pci_phys_to_virt(ULONG address, pci_mem_t *mem);

#endif /* PCI_H */
