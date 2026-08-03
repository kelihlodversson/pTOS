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
#include "endian.h"
#include "kprint.h"
#include "string.h"

#define PCI_MAX_DEVICES 64
#define PCI_MAX_BUSES 8
#define PCI_DEVICES_PER_BUS 32
#define PCI_FUNCTIONS_PER_DEVICE 8
#define PCI_HEADER_MULTIFUNCTION 0x80U
#define PCI_HEADER_TYPE_MASK     0x7fU
#define PCI_HEADER_TYPE_DEVICE   0x00U
#define PCI_HEADER_TYPE_BRIDGE   0x01U
#define PCI_CLASS_BRIDGE_PCI 0x060400UL
#define PCI_BRIDGE_BARS          2
#define PCI_BAR_IO              0x00000001UL
#define PCI_BAR_MEM_TYPE_MASK   0x00000006UL
#define PCI_BAR_MEM_TYPE_64     0x00000004UL
#define PCI_BAR_MEM_PREFETCH    0x00000008UL
#define PCI_BAR_IO_MASK         0xfffffffcUL
#define PCI_BAR_MEM_MASK        0xfffffff0UL

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
static BOOL pci_bus_scanned[PCI_MAX_BUSES];
static UBYTE pci_next_bus;
static BOOL pci_table_full_reported;

static pci_device_t *pci_device_from_handle(PCI_HANDLE handle);
static LONG pci_check_reg(UWORD reg, UWORD size);
static LONG pci_read_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG *value);
static LONG pci_write_config_raw(pci_device_t *device, UWORD reg, UWORD size, ULONG value);
static void pci_scan_bus(UBYTE bus);
static void pci_scan_device(UBYTE bus, UBYTE dev);
static pci_device_t *pci_add_function(UBYTE bus, UBYTE dev, UBYTE func);
static void pci_scan_bridge(pci_device_t *device);
static BOOL pci_is_pci_bridge(const pci_device_t *device);
static void pci_decode_bars(pci_device_t *device);
static void pci_decode_bar(pci_device_t *device, UWORD bar);
static UWORD pci_bar_limit(const pci_device_t *device);
static ULONG pci_bar_size(ULONG mask, BOOL io);
static BOOL pci_class_matches(ULONG device_class, ULONG requested_class, ULONG requested_mask);
static void pci_self_check(void);
static void pci_dummy_interrupt(void *param);
static LONG pci_find_resource_for_address(pci_device_t *device, BOOL io, ULONG address, UWORD size, pci_resource_t **resource);
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

    if (bus >= PCI_MAX_BUSES)
        return;
    if (pci_bus_scanned[bus])
        return;

    pci_bus_scanned[bus] = TRUE;
    for (dev = 0; dev < PCI_DEVICES_PER_BUS; dev++)
        pci_scan_device(bus, dev);
}

static void pci_scan_device(UBYTE bus, UBYTE dev)
{
    ULONG value;
    pci_device_t *device;
    UBYTE header_type;
    UBYTE func;

    if ((pci_backend == 0) || (pci_backend->read_config == 0))
        return;

    if (pci_backend->read_config(bus, dev, 0, PCI_CONFIG_VENDOR_ID, 2, &value) != PCI_SUCCESSFUL)
        return;

    if ((UWORD)value == PCI_ANY_VENDOR)
        return;

    device = pci_add_function(bus, dev, 0);
    if (device != 0)
        pci_scan_bridge(device);

    if (pci_backend->read_config(bus, dev, 0, PCI_CONFIG_HEADER_TYPE, 1, &value) != PCI_SUCCESSFUL)
        return;

    header_type = (UBYTE)value;
    if ((header_type & PCI_HEADER_MULTIFUNCTION) == 0U)
        return;

    for (func = 1; func < PCI_FUNCTIONS_PER_DEVICE; func++) {
        if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_VENDOR_ID, 2, &value) == PCI_SUCCESSFUL) {
            if ((UWORD)value != PCI_ANY_VENDOR) {
                device = pci_add_function(bus, dev, func);
                if (device != 0)
                    pci_scan_bridge(device);
            }
        }
    }
}

static BOOL pci_is_pci_bridge(const pci_device_t *device)
{
    if (device == 0)
        return FALSE;
    if ((device->header_type & PCI_HEADER_TYPE_MASK) != PCI_HEADER_TYPE_BRIDGE)
        return FALSE;
    return (device->classcode & PCI_CLASS_CODE_MASK) == PCI_CLASS_BRIDGE_PCI;
}

static void pci_scan_bridge(pci_device_t *device)
{
    ULONG value;
    UBYTE secondary;
    UBYTE subordinate;

    if (!pci_is_pci_bridge(device))
        return;

    if (pci_read_config_raw(device, PCI_CONFIG_SECONDARY_BUS, 1, &value) != PCI_SUCCESSFUL)
        return;
    secondary = (UBYTE)value;

    if (pci_read_config_raw(device, PCI_CONFIG_SUBORDINATE_BUS, 1, &value) != PCI_SUCCESSFUL)
        return;
    subordinate = (UBYTE)value;

    if ((secondary == 0U) || (subordinate < secondary)) {
        if (pci_next_bus >= PCI_MAX_BUSES)
            return;
        secondary = pci_next_bus++;
        subordinate = secondary;
        pci_write_config_raw(device, PCI_CONFIG_PRIMARY_BUS, 1, (ULONG)device->bus);
        pci_write_config_raw(device, PCI_CONFIG_SECONDARY_BUS, 1, (ULONG)secondary);
        pci_write_config_raw(device, PCI_CONFIG_SUBORDINATE_BUS, 1, (ULONG)subordinate);
    }

    pci_scan_bus(secondary);
}

static pci_device_t *pci_add_function(UBYTE bus, UBYTE dev, UBYTE func)
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
        return 0;
    }

    device = &pci_devices[pci_device_count];
    device->handle = (PCI_HANDLE)pci_device_count + 1UL;
    device->bus = bus;
    device->dev = dev;
    device->func = func;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_VENDOR_ID, 2, &value) != PCI_SUCCESSFUL)
        return 0;
    device->vendor = (UWORD)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_DEVICE_ID, 2, &value) != PCI_SUCCESSFUL)
        return 0;
    device->device = (UWORD)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_BASE_CLASS, 1, &base) != PCI_SUCCESSFUL)
        return 0;
    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_SUBCLASS, 1, &subclass) != PCI_SUCCESSFUL)
        return 0;
    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_PROGIF, 1, &progif) != PCI_SUCCESSFUL)
        return 0;
    device->classcode = ((base & 0xffUL) << 16) | ((subclass & 0xffUL) << 8) | (progif & 0xffUL);

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_HEADER_TYPE, 1, &value) != PCI_SUCCESSFUL)
        return 0;
    device->header_type = (UBYTE)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_INTERRUPT_LINE, 1, &value) != PCI_SUCCESSFUL)
        value = 0UL;
    device->interrupt_line = (UBYTE)value;

    if (pci_backend->read_config(bus, dev, func, PCI_CONFIG_INTERRUPT_PIN, 1, &value) != PCI_SUCCESSFUL)
        value = 0UL;
    device->interrupt_pin = (UBYTE)value;

    pci_decode_bars(device);

    device->callback = 0;
    device->used = 0;
    pci_device_count++;
    return device;
}

static void pci_decode_bars(pci_device_t *device)
{
    UWORD bar;
    UWORD last;
    UWORD limit;
    ULONG original;
    BOOL skip_next;

    memset(device->resources, 0, sizeof(device->resources));

    limit = pci_bar_limit(device);
    for (bar = 0; bar < limit; bar++) {
        skip_next = FALSE;
        if (pci_read_config_raw(device, PCI_CONFIG_BAR0 + (bar * 4U), 4, &original) == PCI_SUCCESSFUL) {
            if (((original & PCI_BAR_IO) == 0UL) &&
                ((original & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_TYPE_64) &&
                (bar + 1U < limit))
                skip_next = TRUE;
        }
        pci_decode_bar(device, bar);
        if (skip_next)
            bar++;
    }

    last = PCI_MAX_BARS;
    for (bar = 0; bar < PCI_MAX_BARS; bar++) {
        if (device->resources[bar].length != 0UL)
            last = bar;
    }

    if (last < PCI_MAX_BARS)
        device->resources[last].flags |= PCI_RESOURCE_LAST;
}

static UWORD pci_bar_limit(const pci_device_t *device)
{
    UBYTE type;

    type = device->header_type & PCI_HEADER_TYPE_MASK;
    if (type == PCI_HEADER_TYPE_DEVICE)
        return PCI_MAX_BARS;
    if (type == PCI_HEADER_TYPE_BRIDGE)
        return PCI_BRIDGE_BARS;
    return 0;
}

static void pci_decode_bar(pci_device_t *device, UWORD bar)
{
    UWORD reg;
    ULONG original;
    ULONG mask;
    ULONG masked_address;
    ULONG phys_address;
    BOOL io;
    pci_resource_t *resource;

    reg = PCI_CONFIG_BAR0 + (bar * 4U);

    if (pci_read_config_raw(device, reg, 4, &original) != PCI_SUCCESSFUL)
        return;
    if (original == 0UL)
        return;

    if (pci_write_config_raw(device, reg, 4, 0xffffffffUL) != PCI_SUCCESSFUL)
        return;
    if (pci_read_config_raw(device, reg, 4, &mask) != PCI_SUCCESSFUL) {
        pci_write_config_raw(device, reg, 4, original);
        return;
    }
    pci_write_config_raw(device, reg, 4, original);

    if (mask == 0UL)
        return;

    io = (original & PCI_BAR_IO) != 0UL;
    masked_address = original & (io ? PCI_BAR_IO_MASK : PCI_BAR_MEM_MASK);
    mask &= io ? PCI_BAR_IO_MASK : PCI_BAR_MEM_MASK;
    if (mask == 0UL)
        return;

    if ((pci_backend != 0) && (pci_backend->bus_to_phys != 0)) {
        if (pci_backend->bus_to_phys(masked_address, io, &phys_address) != PCI_SUCCESSFUL)
            return;
    } else {
        phys_address = masked_address;
    }

    resource = &device->resources[bar];
    resource->flags = PCI_RESOURCE_8BIT | PCI_RESOURCE_16BIT |
                      PCI_RESOURCE_32BIT | PCI_RESOURCE_ORDER_INTEL;
    if (io)
        resource->flags |= PCI_RESOURCE_IO;
    resource->start = masked_address;
    resource->length = pci_bar_size(mask, io);
    resource->offset = phys_address - masked_address;
    resource->dmaoffset = 0UL;
}

static ULONG pci_bar_size(ULONG mask, BOOL io)
{
    ULONG masked_size;

    masked_size = mask & (io ? PCI_BAR_IO_MASK : PCI_BAR_MEM_MASK);
    return (~masked_size) + 1UL;
}

LONG pci_init(void)
{
    LONG ret;

    pci_device_count = 0;
    pci_table_full_reported = FALSE;
    memset(pci_devices, 0, sizeof(pci_devices));
    memset(pci_bus_scanned, 0, sizeof(pci_bus_scanned));
    pci_next_bus = 1U;
    pci_backend = pci_backend_get();

    if ((pci_backend == 0) || (pci_backend->init == 0))
        return PCI_FUNC_NOT_SUPPORTED;

    ret = pci_backend->init();
    if (ret != PCI_SUCCESSFUL)
        return ret;

    pci_scan_bus(0);
    KINFO(("pci: %u device(s) found\n", pci_device_count));
    pci_self_check();

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

static BOOL pci_class_matches(ULONG device_class, ULONG requested_class, ULONG requested_mask)
{
    ULONG mask;

    mask = PCI_CLASS_CODE_MASK;
    if ((requested_mask & PCI_CLASS_MASK_BASE) != 0UL)
        mask &= 0x0000ffffUL;
    if ((requested_mask & PCI_CLASS_MASK_SUBCLASS) != 0UL)
        mask &= 0x00ff00ffUL;
    if ((requested_mask & PCI_CLASS_MASK_PROGIF) != 0UL)
        mask &= 0x00ffff00UL;

    return (device_class & mask) == (requested_class & mask & PCI_CLASS_CODE_MASK);
}

static void pci_dummy_interrupt(void *param)
{
    (void)param;
}

static void pci_self_check(void)
{
    PCI_HANDLE handle;
    PCI_HANDLE all_handle;
    pci_mem_t mem;
    ULONG value;
    UWORD word;
    LONG ret;

    if (pci_device_count == 0)
        return;

    mem.address = 0UL;
    mem.length = 0UL;
    handle = pci_devices[0].handle;

    ret = pci_find_device(pci_devices[0].vendor, pci_devices[0].device, 0, &handle);
    if ((ret != PCI_SUCCESSFUL) || (handle != pci_devices[0].handle))
        KINFO(("pci: self-check exact lookup failed (%ld)\n", ret));

    ret = pci_find_device(PCI_ANY_VENDOR, 0, 0, &all_handle);
    if ((ret != PCI_SUCCESSFUL) || (all_handle != pci_devices[0].handle))
        KINFO(("pci: self-check wildcard lookup failed (%ld)\n", ret));

    ret = pci_find_classcode(pci_devices[0].classcode, PCI_CLASS_MASK_PROGIF, 0, &handle);
    if (ret != PCI_SUCCESSFUL)
        KINFO(("pci: self-check class lookup failed (%ld)\n", ret));

    ret = pci_read_config_long(PCI_HANDLE_NONE, PCI_CONFIG_VENDOR_ID, &value);
    if (ret != PCI_BAD_HANDLE)
        KINFO(("pci: self-check invalid handle returned %ld\n", ret));

    ret = pci_read_config_word(pci_devices[0].handle, 0xffU, &word);
    if (ret != PCI_BAD_REGISTER_NUMBER)
        KINFO(("pci: self-check invalid register returned %ld\n", ret));

    ret = pci_hook_interrupt(pci_devices[0].handle, pci_dummy_interrupt, 0);
    if ((ret != PCI_SUCCESSFUL) && (ret != PCI_FUNC_NOT_SUPPORTED))
        KINFO(("pci: self-check interrupt hook returned %ld\n", ret));
    if (ret == PCI_SUCCESSFUL)
        pci_unhook_interrupt(pci_devices[0].handle);

    ret = pci_virt_to_bus(pci_devices[0].handle, 0x40000000UL, &mem);
    if ((ret != PCI_SUCCESSFUL) || (mem.address != 0x40000000UL))
        KINFO(("pci: self-check RAM virt-to-bus failed (%ld, %08lx)\n", ret, mem.address));
}

LONG pci_find_classcode(ULONG classcode, ULONG mask, UWORD index, PCI_HANDLE *handle)
{
    UWORD i;
    UWORD found;

    if (handle == 0)
        return PCI_GENERAL_ERROR;

    *handle = PCI_HANDLE_NONE;

    found = 0;
    for (i = 0; i < pci_device_count; i++) {
        if (pci_class_matches(pci_devices[i].classcode, classcode, mask)) {
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

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;

    if ((bar >= PCI_MAX_BARS) || (resource == 0))
        return PCI_BAD_RESOURCE;

    if (device->resources[bar].length == 0UL)
        return PCI_BAD_RESOURCE;

    *resource = device->resources[bar];
    return PCI_SUCCESSFUL;
}

static LONG pci_find_resource_for_address(pci_device_t *device, BOOL io, ULONG address, UWORD size, pci_resource_t **resource)
{
    UWORD bar;
    ULONG end;
    ULONG resource_end;
    pci_resource_t *candidate;

    if ((device == 0) || (resource == 0) || (size == 0U))
        return PCI_BAD_RESOURCE;

    if (((size == 2U) && ((address & 1UL) != 0UL)) ||
        ((size == 4U) && ((address & 3UL) != 0UL)))
        return PCI_BAD_RESOURCE;

    end = address + (ULONG)size;
    if (end < address)
        return PCI_BAD_RESOURCE;

    for (bar = 0; bar < PCI_MAX_BARS; bar++) {
        candidate = &device->resources[bar];
        if (candidate->length != 0UL) {
            if (((candidate->flags & PCI_RESOURCE_IO) != 0U) == io) {
                resource_end = candidate->start + candidate->length;
                if (resource_end >= candidate->start) {
                    if ((address >= candidate->start) && (end <= resource_end)) {
                        *resource = candidate;
                        return PCI_SUCCESSFUL;
                    }
                }
            }
        }
    }

    return PCI_BAD_RESOURCE;
}

static LONG pci_read_bus_byte(PCI_HANDLE handle, ULONG address, BOOL io, UBYTE *value)
{
    pci_device_t *device;
    pci_resource_t *resource;
    volatile UBYTE *ptr;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_find_resource_for_address(device, io, address, 1U, &resource) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    ptr = (volatile UBYTE *)(address + resource->offset);
    *value = *ptr;
    return PCI_SUCCESSFUL;
}

static LONG pci_read_bus_word(PCI_HANDLE handle, ULONG address, BOOL io, UWORD *value)
{
    pci_device_t *device;
    pci_resource_t *resource;
    volatile UWORD *ptr;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_find_resource_for_address(device, io, address, 2U, &resource) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    ptr = (volatile UWORD *)(address + resource->offset);
    *value = le2cpu16(*ptr);
    return PCI_SUCCESSFUL;
}

static LONG pci_read_bus_long(PCI_HANDLE handle, ULONG address, BOOL io, ULONG *value)
{
    pci_device_t *device;
    pci_resource_t *resource;
    volatile ULONG *ptr;

    if (value == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_find_resource_for_address(device, io, address, 4U, &resource) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    ptr = (volatile ULONG *)(address + resource->offset);
    *value = le2cpu32(*ptr);
    return PCI_SUCCESSFUL;
}

static LONG pci_write_bus_byte(PCI_HANDLE handle, ULONG address, BOOL io, UBYTE value)
{
    pci_device_t *device;
    pci_resource_t *resource;
    volatile UBYTE *ptr;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_find_resource_for_address(device, io, address, 1U, &resource) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    ptr = (volatile UBYTE *)(address + resource->offset);
    *ptr = value;
    return PCI_SUCCESSFUL;
}

static LONG pci_write_bus_word(PCI_HANDLE handle, ULONG address, BOOL io, UWORD value)
{
    pci_device_t *device;
    pci_resource_t *resource;
    volatile UWORD *ptr;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_find_resource_for_address(device, io, address, 2U, &resource) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    ptr = (volatile UWORD *)(address + resource->offset);
    *ptr = cpu2le16(value);
    return PCI_SUCCESSFUL;
}

static LONG pci_write_bus_long(PCI_HANDLE handle, ULONG address, BOOL io, ULONG value)
{
    pci_device_t *device;
    pci_resource_t *resource;
    volatile ULONG *ptr;

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (pci_find_resource_for_address(device, io, address, 4U, &resource) != PCI_SUCCESSFUL)
        return PCI_BAD_RESOURCE;
    ptr = (volatile ULONG *)(address + resource->offset);
    *ptr = cpu2le32(value);
    return PCI_SUCCESSFUL;
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

    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    if (callback != 0)
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
        return PCI_GENERAL_ERROR;
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
    LONG ret;
    pci_device_t *device;

    if (mem == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    bus_address = address;
    if ((pci_backend != 0) && (pci_backend->phys_to_bus != 0)) {
        ret = pci_backend->phys_to_bus(address, FALSE, &bus_address);
        if ((ret != PCI_SUCCESSFUL) && (ret != PCI_BAD_RESOURCE))
            return PCI_GENERAL_ERROR;
    }
    mem->address = bus_address;
    mem->length = 0xffffffffUL - address;
    return PCI_SUCCESSFUL;
}

LONG pci_bus_to_virt(PCI_HANDLE handle, ULONG address, pci_mem_t *mem)
{
    ULONG phys_address;
    LONG ret;
    pci_device_t *device;

    if (mem == 0)
        return PCI_GENERAL_ERROR;
    device = pci_device_from_handle(handle);
    if (device == 0)
        return PCI_BAD_HANDLE;
    phys_address = address;
    if ((pci_backend != 0) && (pci_backend->bus_to_phys != 0)) {
        ret = pci_backend->bus_to_phys(address, FALSE, &phys_address);
        if (ret == PCI_BAD_RESOURCE)
            return ret;
        if (ret != PCI_SUCCESSFUL)
            return PCI_GENERAL_ERROR;
    }
    mem->address = phys_address;
    mem->length = 0xffffffffUL - address;
    return PCI_SUCCESSFUL;
}

LONG pci_virt_to_phys(ULONG address, pci_mem_t *mem)
{
    if (mem == 0)
        return PCI_GENERAL_ERROR;
    mem->address = address;
    mem->length = 0xffffffffUL - address;
    return PCI_SUCCESSFUL;
}

LONG pci_phys_to_virt(ULONG address, pci_mem_t *mem)
{
    if (mem == 0)
        return PCI_GENERAL_ERROR;
    mem->address = address;
    mem->length = 0xffffffffUL - address;
    return PCI_SUCCESSFUL;
}

#endif /* CONF_WITH_PCI */
