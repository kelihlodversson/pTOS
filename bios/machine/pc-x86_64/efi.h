/*
 * efi.h - minimal UEFI type and protocol definitions
 *
 * Just enough of the UEFI specification (2.10) to boot through it: get the
 * boot-time memory map, find out where our own image was loaded, and exit
 * boot services.  Not a general-purpose EFI header -- fields we never call
 * or read are kept as opaque VOID* placeholders (correctly sized, since
 * every UEFI table entry is either a pointer or a function pointer, both
 * 8 bytes on x86-64) purely to keep the fields we DO use at their correct,
 * spec-mandated offsets.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_EFI_H
#define PC_X86_64_EFI_H

#include "portab.h"

/* UEFI firmware call convention: Microsoft x64, not this codebase's SysV. */
#define EFIAPI __attribute__((ms_abi))

typedef UQUAD EFI_STATUS;
typedef void *EFI_HANDLE;
typedef UWORD CHAR16;
typedef UBYTE BOOLEAN;

#define EFI_SUCCESS 0
/* The high bit of EFI_STATUS marks an error, on both 32- and 64-bit UINTN. */
#define EFI_ERROR_BIT 0x8000000000000000ULL

typedef struct {
    ULONG Data1;
    UWORD Data2;
    UWORD Data3;
    UBYTE Data4[8];
} EFI_GUID;

typedef struct {
    UQUAD Signature;
    ULONG Revision;
    ULONG HeaderSize;
    ULONG CRC32;
    ULONG Reserved;
} EFI_TABLE_HEADER;

/*
 * EFI_MEMORY_DESCRIPTOR (UEFI spec 7.2).  Callers must stride through an
 * array of these by the DescriptorSize GetMemoryMap() returns, never by
 * sizeof(EFI_MEMORY_DESCRIPTOR): the spec explicitly reserves the right to
 * grow this struct, and real firmware (including OVMF) already does.
 */
typedef struct {
    ULONG Type;
    UQUAD PhysicalStart;
    UQUAD VirtualStart;
    UQUAD NumberOfPages;
    UQUAD Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UQUAD *MemoryMapSize,
                                                 EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                                 UQUAD *MapKey,
                                                 UQUAD *DescriptorSize,
                                                 ULONG *DescriptorVersion);

typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle, UQUAD MapKey);

typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle,
                                                  EFI_GUID *Protocol,
                                                  void **Interface);

/*
 * EFI_BOOT_SERVICES (UEFI spec 4.4).  Field order and count matter: only
 * GetMemoryMap, ExitBootServices and HandleProtocol are given real
 * prototypes, but every field ahead of them must still be present (as a
 * same-sized VOID*) so those three land at their real offsets.
 */
typedef struct {
    EFI_TABLE_HEADER Hdr;

    void *RaiseTPL;
    void *RestoreTPL;

    void *AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    void *AllocatePool;
    void *FreePool;

    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;

    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;

    /* Fields beyond this point are never referenced, so are left out. */
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    ULONG FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    void *ConOut;
    EFI_HANDLE StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    /* Fields beyond this point are never referenced, so are left out. */
} EFI_SYSTEM_TABLE;

/* EFI_LOADED_IMAGE_PROTOCOL_GUID (UEFI spec 9.1). */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

/*
 * EFI_LOADED_IMAGE_PROTOCOL (UEFI spec 9.1): only ImageBase is used, to
 * learn where UEFI actually loaded this (relocatable) image, but every
 * field ahead of it is needed to put ImageBase at its real offset.
 */
typedef struct {
    ULONG Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;

    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;

    ULONG LoadOptionsSize;
    void *LoadOptions;

    void *ImageBase;
    UQUAD ImageSize;
    ULONG ImageCodeType;
    ULONG ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

#endif /* PC_X86_64_EFI_H */
