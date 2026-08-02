/*
 * pci_core.c - native pTOS PCI access layer
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_PCI

#include "pci.h"
#include "pci_backend.h"
#include "kprint.h"
#include "string.h"

#define PCI_MAX_DEVICES 64
#define PCI_DEVICES_PER_BUS 32
#define PCI_FUNCTIONS_PER_DEVICE 8
#define PCI_HEADER_MULTIFUNCTION 0x80U

typedef struct {
    PCI_HANDLE handle;
    UBYTE bus;
    UBYTE dev;
    UBYTE func;
    UWORD vendor;
    UWORD device;
    ULONG classcode;
    UBYTE header_type;
    UBYTE interrupt_line;
    UBYTE interrupt_pin;
    pci_resource_t resources[PCI_MAX_BARS];
    pci_card_callback_t callback;
    LONG used;
} pci_device_t;

static const pci_backend_t *pci_backend;
static pci_device_t pci_devices[PCI_MAX_DEVICES];
static UWORD pci_device_count;
static BOOL pci_table_full_reported;

static pci_device_t *pci_device_from_handle(PCI_HANDLE handle);
static LONG pci_check_reg(UWORD reg, UWORD size);
static LONG pci_read_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG *value);
static LONG pci_write_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG value);
static void pci_scan_bus(UBYTE bus);
static void pci_scan_device(UBYTE bus, UBYTE dev);
static void pci_add_function(UBYTE bus, UBYTE dev, UBYTE func);
static BOOL pci_class_matches(ULONG device_class, ULONG requested_class);
static LONG pci_check_resource_access(pci_device_t *device, ULONG address, ULONG size, BOOL io);
static LONG pci_read_bus_byte(PCI_HANDLE handle, ULONG address, BOOL io, UBYTE *value);
static LONG pci_read_bus_word(PCI_HANDLE handle, ULONG address, BOOL io, UWORD *value);
static LONG pci_read_bus_long(PCI_HANDLE handle, ULONG address, BOOL io, ULONG *value);
static LONG pci_write_bus_byte(PCI_HANDLE handle, ULONG address, BOOL io, UBYTE value);
static LONG pci_write_bus_word(PCI_HANDLE handle, ULONG address, BOOL io, UWORD value);
static LONG pci_write_bus_long(PCI_HANDLE handle, ULONG address, BOOL io, ULONG value);

static pci_device_t *pci_device_from_handle(PCI_HANDLE handle)
{
    ULONG index;

    if (handle == PCI_HANDLE_NONE)
        return 0;

    index = handle - 1UL;
    if (index >= (ULONG)pci_device_count)
        return 0;

    if (pci_devices[index].handle != handle)
        return 0;

    return &pci_devices[index];
}

static LONG pci_check_reg(UWORD reg, UWORD size)
{
    if (reg >= 256U)
        return PCI_BAD_REGISTER_NUMBER;

    if ((ULONG)reg + (ULONG)size > 256UL)
        return PCI_BAD_REGISTER_NUMBER;

    if ((size == 2U) && ((reg & 1U) != 0U))
        return PCI_BAD_REGISTER_NUMBER;

    if ((size == 4U) && ((reg & 3U) != 0U))
        return PCI_BAD_REGISTER_NUMBER;

    if ((size != 1U) && (size != 2U) && (size != 4U))
        return PCI_BAD_REGISTER_NUMBER;

    return PCI_SUCCESSFUL;
}

static LONG pci_read_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG *value)
{
    LONG ret;

    if ((device == 0) || (value == 0))
        return PCI_GENERAL_ERROR;

    ret = pci_check_reg(reg, size);
    if (ret != PCI_SUCCESSFUL)
        return ret;

    if ((pci_backend == 0) || (pci_backend->read_config == 0))
        return PCI_FUNC_NOT_SUPPORTED;

    return pci_backend->read_config(device->bus, device->dev, device->func, reg, size, value);
}

static LONG pci_write_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG value)
{
    LONG ret;

    if (device == 0)
        return PCI_GENERAL_ERROR;

    ret = pci_check_reg(reg, size);
    if (ret != PCI_SUCCESSFUL)
        return ret;

    if ((pci_backend == 0) || (pci_backend->write_config == 0))
        return PCI_FUNC_NOT_SUPPORTED;

    return pci_backend->write_config(device->bus, device->dev, device->func, reg, size, value);
}

static void pci_scan_bus(UBYTE bus)
{
    UBYTE dev;

    for (dev = 0; dev < PCI_DEVICES_PER_BUS; dev++)
        pci_scan_device(bus, dev);
}

static void pci_scan_device(UBYTE bus, UBYTE dev)
{
    ULONG value;
    UBYTE header_type;
    UBYTE func;

    if ((pci_backend == 0) || (pci_backend->read_config == 0))
        return;

    if (pci_backend->read_config(bus, dev, 0, PCI_CONFIG_VENDOR_ID, 2, &value) != PCI_SUCCESSFUL)
        return;

    if ((UWORD)value == PCI_ANY_VENDOR)
        return;

    pci_add_function(bus, dev, 0);

    if (pci_backend->read_config(bus, dev, 0, PCI_CONFIG_HEADER_TYPE, 1, &value) != PCI_SUCCESSFUL)
        return;

    header_type = (UBYTE)value;
    if ((header_type & PCI_HEADER_MULTIFUNCTION) == 0U)
        return;

    for (func = 1; func < PCI_FUNCTIONS_PER_DEVICE; func++) {
        if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_VENDOR_ID, 2, &value) == PCI_SUCCESSFUL) {
            if ((UWORD)value != PCI_ANY_VENDOR)
                pci_add_function(bus, dev, func);
        }
    }
}

static void pci_add_function(UBYTE bus, UBYTE dev, UBYTE func)
{
    ULONG value;
    ULONG base;
    ULONG subclass;
    ULONG progif;
    pci_device_t *device;

    if (pci_device_count >= PCI_MAX_DEVICES) {
        if (!pci_table_full_reported) {
            KINFO(("pci: device table full\n"));
            pci_table_full_reported = TRUE;
        }
        return;
    }

    device = &pci_devices[pci_device_count];
    device->handle = (PCI_HANDLE)pci_device_count + 1UL;
    device->bus = bus;
    device->dev = dev;
    device->func = func;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_VENDOR_ID, 2, &value) != PCI_SUCCESSFUL)
        return;
    device->vendor = (UWORD)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_DEVICE_ID, 2, &value) != PCI_SUCCESSFUL)
        return;
    device->device = (UWORD)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_BASE_CLASS, 1, &base) != PCI_SUCCESSFUL)
        return;
    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_SUBCLASS, 1, &subclass) != PCI_SUCCESSFUL)
        return;
    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_PROGIF, 1, &progif) != PCI_SUCCESSFUL)
        return;
    device->classcode = ((base & 0xffUL) << 16) | ((subclass & 0xffUL) << 8) | (progif & 0xffUL);

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_HEADER_TYPE, 1, &value) != PCI_SUCCESSFUL)
        return;
    device->header_type = (UBYTE)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_INTERRUPT_LINE, 1, &value) != PCI_SUCCESSFUL)
        value = 0UL;
    device->interrupt_line = (UBYTE)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_INTERRUPT_PIN, 1, &value) != PCI_SUCCESSFUL)
        value = 0UL;
    device->interrupt_pin = (UBYTE)value;

    device->callback = 0;
    device->used = 0;
    pci_device_count++;
}

LONG pci_init(void)
{
    LONG ret;

    pci_device_count = 0;
    pci_table_full_reported = FALSE;
    memset(pci_devices, 0, sizeof(pci_devices));
    pci_backend = pci_backend_get();

    if ((pci_backend == 0) || (pci_backend->init == 0))
        return PCI_FUNC_NOT_SUPPORTED;

    ret = pci_backend->init();
    if (ret != PCI_SUCCESSFUL)
        return ret;

    pci_scan_bus(0);
    KINFO(("pci: %u device(s) found\n", pci_device_count));

    return PCI_SUCCESSFUL;
}

LONG pci_find_device(UWORD vendor, UWORD device, UWORD index, PCI_HANDLE *handle)
{
    UWORD i;
    UWORD found;

    if (handle == 0)
        return PCI_GENERAL_ERROR;

    *handle = PCI_HANDLE_NONE;

    if (vendor == PCI_ANY_VENDOR)
        device = 0;

    found = 0;
    for (i = 0; i < pci_device_count; i++) {
        if ((vendor == PCI_ANY_VENDOR) ||
            ((pci_devices[i].vendor == vendor) && (pci_devices[i].device == device))) {
            if (found == index) {
                *handle = pci_devices[i].handle;
                return PCI_SUCCESSFUL;
            }
            found++;
        }
    }

    return PCI_DEVICE_NOT_FOUND;
}

static BOOL pci_class_matches(ULONG device_class, ULONG requested_class)
{
    ULONG mask;

    mask = PCI_CLASS_CODE_MASK;
    if ((requested_class & PCI_CLASS_MASK_BASE) != 0UL)
        mask &= 0x0000ffffUL;
    if ((requested_class & PCI_CLASS_MASK_SUBCLASS) != 0UL)
        mask &= 0x00ff00ffUL;
    if ((requested_class & PCI_CLASS_MASK_PROGIF) != 0UL)
        mask &= 0x00ffff00UL;

    return (device_class & mask) == (requested_class & mask & PCI_CLASS_CODE_MASK);
}

LONG pci_find_classcode(ULONG classcode, UWORD index, PCI_HANDLE *handle)
{
    UWORD i;
    UWORD found;

    if (handle == 0)
        return PCI_GENERAL_ERROR;

    *handle = PCI_HANDLE_NONE;

    found = 0;
    for (i = 0; i < pci_device_count; i++) {
        if (pci_class_matches(pci_devices[i].classcode, classcode)) {
            if (found == index) {
                *handle = pci_devices[i].handle;
                return PCI_SUCCESSFUL;
            }
            found++;
        }
    }

    return PCI_DEVICE_NOT_FOUND;
}

LONG pci_read_config_byte(PCI_HANDLE handle, UWORD reg, UBYTE *value)
{
    ULONG raw;
    LONG ret;
    pci_device_t *device;

    if (value == 0)
        return PCI_GENERAL_ERROR;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;

    ret = pci_read_config_raw(device, reg, 1, &raw);
    if (ret == PCI_SUCCESSFUL)
        *value = (UBYTE)raw;
    return ret;
}

LONG pci_read_config_word(PCI_HANDLE handle, UWORD reg, UWORD *value)
{
    ULONG raw;
    LONG ret;
    pci_device_t *device;

    if (value == 0)
        return PCI_GENERAL_ERROR;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;

    ret = pci_read_config_raw(device, reg, 2, &raw);
    if (ret == PCI_SUCCESSFUL)
        *value = (UWORD)raw;
    return ret;
}

LONG pci_read_config_long(PCI_HANDLE handle, UWORD reg, ULONG *value)
{
    pci_device_t *device;

    if (value == 0)
        return PCI_GENERAL_ERROR;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    return pci_read_config_raw(device, reg, 4, value);
}

LONG pci_write_config_byte(PCI_HANDLE handle, UWORD reg, UBYTE value)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    return pci_write_config_raw(device, reg, 1, (ULONG)value);
}

LONG pci_write_config_word(PCI_HANDLE handle, UWORD reg, UWORD value)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    return pci_write_config_raw(device, reg, 2, (ULONG)value);
}

LONG pci_write_config_long(PCI_HANDLE handle, UWORD reg, ULONG value)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    return pci_write_config_raw(device, reg, 4, value);
}

LONG pci_get_resource(PCI_HANDLE handle, UWORD bar, pci_resource_t *resource)
{
    pci_device_t *device;

    if (resource == 0)
        return PCI_GENERAL_ERROR;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;

    if (bar >= PCI_MAX_BARS)
        return PCI_BAD_RESOURCE;

    if (device->resources[bar].length == 0UL)
        return PCI_BAD_RESOURCE;

    *resource = device->resources[bar];
    return PCI_SUCCESSFUL;
}

static LONG pci_check_resource_access(pci_device_t *device, ULONG address, ULONG size, BOOL io)
{
    UWORD bar;
    ULONG end;
    ULONG resource_end;
    pci_resource_t *resource;

    if (size == 0UL)
        return PCI_BAD_RESOURCE;

    end = address + size - 1UL;
    if (end < address)
        return PCI_BAD_RESOURCE;

    for (bar = 0; bar < PCI_MAX_BARS; bar++) {
        resource = &device->resources[bar];
        if (resource->length != 0UL) {
            if (((resource->flags & PCI_RESOURCE_IO) != 0U) == io) {
                resource_end = resource->start + resource->length - 1UL;
                if (resource_end >= resource->start) {
                    if ((address >= resource->start) && (end <= resource_end))
                        return PCI_SUCCESSFUL;
                }
            }
        }
    }

    return PCI_BAD_RESOURCE;
}

static LONG pci_read_bus_byte(PCI_HANDLE handle, ULONG address, BOOL io, UBYTE *value)
{
    pci_device_t *device;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_check_resource_access(device, address, 1UL, io) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG pci_read_bus_word(PCI_HANDLE handle, ULONG address, BOOL io, UWORD *value)
{
    pci_device_t *device;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_check_resource_access(device, address, 2UL, io) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG pci_read_bus_long(PCI_HANDLE handle, ULONG address, BOOL io, ULONG *value)
{
    pci_device_t *device;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_check_resource_access(device, address, 4UL, io) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG pci_write_bus_byte(PCI_HANDLE handle, ULONG address, BOOL io, UBYTE value)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_check_resource_access(device, address, 1UL, io) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG pci_write_bus_word(PCI_HANDLE handle, ULONG address, BOOL io, UWORD value)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_check_resource_access(device, address, 2UL, io) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    return PCI_FUNC_NOT_SUPPORTED;
}

static LONG pci_write_bus_long(PCI_HANDLE handle, ULONG address, BOOL io, ULONG value)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_check_resource_access(device, address, 4UL, io) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    return PCI_FUNC_NOT_SUPPORTED;
}

LONG pci_read_mem_byte(PCI_HANDLE handle, ULONG address, UBYTE *value)
{
    return pci_read_bus_byte(handle, address, FALSE, value);
}

LONG pci_read_mem_word(PCI_HANDLE handle, ULONG address, UWORD *value)
{
    return pci_read_bus_word(handle, address, FALSE, value);
}

LONG pci_read_mem_long(PCI_HANDLE handle, ULONG address, ULONG *value)
{
    return pci_read_bus_long(handle, address, FALSE, value);
}

LONG pci_write_mem_byte(PCI_HANDLE handle, ULONG address, UBYTE value)
{
    return pci_write_bus_byte(handle, address, FALSE, value);
}

LONG pci_write_mem_word(PCI_HANDLE handle, ULONG address, UWORD value)
{
    return pci_write_bus_word(handle, address, FALSE, value);
}

LONG pci_write_mem_long(PCI_HANDLE handle, ULONG address, ULONG value)
{
    return pci_write_bus_long(handle, address, FALSE, value);
}

LONG pci_read_io_byte(PCI_HANDLE handle, ULONG address, UBYTE *value)
{
    return pci_read_bus_byte(handle, address, TRUE, value);
}

LONG pci_read_io_word(PCI_HANDLE handle, ULONG address, UWORD *value)
{
    return pci_read_bus_word(handle, address, TRUE, value);
}

LONG pci_read_io_long(PCI_HANDLE handle, ULONG address, ULONG *value)
{
    return pci_read_bus_long(handle, address, TRUE, value);
}

LONG pci_write_io_byte(PCI_HANDLE handle, ULONG address, UBYTE value)
{
    return pci_write_bus_byte(handle, address, TRUE, value);
}

LONG pci_write_io_word(PCI_HANDLE handle, ULONG address, UWORD value)
{
    return pci_write_bus_word(handle, address, TRUE, value);
}

LONG pci_write_io_long(PCI_HANDLE handle, ULONG address, ULONG value)
{
    return pci_write_bus_long(handle, address, TRUE, value);
}

LONG pci_hook_interrupt(PCI_HANDLE handle, pci_interrupt_handler_t handler, void *param)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (handler == 0)
        return PCI_GENERAL_ERROR;
    if ((pci_backend == 0) || (pci_backend->hook_interrupt == 0))
        return PCI_FUNC_NOT_SUPPORTED;
    return pci_backend->hook_interrupt(handle, device->interrupt_line, handler, param);
}

LONG pci_unhook_interrupt(PCI_HANDLE handle)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if ((pci_backend == 0) || (pci_backend->unhook_interrupt == 0))
        return PCI_FUNC_NOT_SUPPORTED;
    return pci_backend->unhook_interrupt(handle, device->interrupt_line);
}

LONG pci_get_card_used(PCI_HANDLE handle, pci_card_callback_t *callback)
{
    pci_device_t *device;

    if (callback == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    *callback = device->callback;
    return device->used;
}

LONG pci_set_card_used(PCI_HANDLE handle, pci_card_callback_t callback, LONG state)
{
    pci_device_t *device;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if ((state < 0) || (state > 3))
        return PCI_SET_FAILED;
    if (state == 2)
        device->callback = callback;
    else
        device->callback = 0;
    device->used = state;
    return PCI_SUCCESSFUL;
}

LONG pci_get_pagesize(ULONG *pagesize)
{
    if (pagesize == 0)
        return PCI_GENERAL_ERROR;
    *pagesize = 0UL;
    return PCI_SUCCESSFUL;
}

LONG pci_virt_to_bus(PCI_HANDLE handle, ULONG address, pci_mem_t *mem)
{
    ULONG bus_address;
    pci_device_t *device;

    if (mem == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    bus_address = address;
    if ((pci_backend != 0) && (pci_backend->phys_to_bus != 0)) {
        if (pci_backend->phys_to_bus(address, FALSE, &bus_address) != PCI_SUCCESSFUL)
            return PCI_GENERAL_ERROR;
    }
    mem->address = bus_address;
    mem->length = 0xffffffffUL;
    return PCI_SUCCESSFUL;
}

LONG pci_bus_to_virt(PCI_HANDLE handle, ULONG address, pci_mem_t *mem)
{
    ULONG phys_address;
    pci_device_t *device;

    if (mem == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    phys_address = address;
    if ((pci_backend != 0) && (pci_backend->bus_to_phys != 0)) {
        if (pci_backend->bus_to_phys(address, FALSE, &phys_address) != PCI_SUCCESSFUL)
            return PCI_GENERAL_ERROR;
    }
    mem->address = phys_address;
    mem->length = 0xffffffffUL;
    return PCI_SUCCESSFUL;
}

LONG pci_virt_to_phys(ULONG address, pci_mem_t *mem)
{
    if (mem == 0)
        return PCI_GENERAL_ERROR;
    mem->address = address;
    mem->length = 0xffffffffUL;
    return PCI_SUCCESSFUL;
}

LONG pci_phys_to_virt(ULONG address, pci_mem_t *mem)
{
    if (mem == 0)
        return PCI_GENERAL_ERROR;
    mem->address = address;
    mem->length = 0xffffffffUL;
    return PCI_SUCCESSFUL;
}

#endif /* CONF_WITH_PCI */
