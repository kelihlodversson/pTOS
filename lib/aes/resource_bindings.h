/* Bind the ordinary portable loader/address units to private user state. */
#ifndef LIBAES_RESOURCE_BINDINGS_H
#define LIBAES_RESOURCE_BINDINGS_H
#include "resource_context.h"
#include "gemlayout.h"
#define RSLOAD_COLOUR_ICONS 1
#define RS_ADDR_SLOT_SIZE sizeof(void *)
#define rs_hdr (aes_resource->native.ap_rscmem)
#define rs_global (&aes_resource->native)
#define rs_readit aes_resource_read
#define fix_objects aes_resource_fix
#define get_addr aes_resource_addr
#define get_sub aes_resource_sub
#define get_ciconblkptr aes_resource_cicons
#define dos_alloc_anyram aes_resource_alloc
#define dos_free aes_resource_release
#define dos_read aes_resource_read_file
#define dos_lseek aes_resource_seek
#define rs_obfix(tree,obj) rs_obfix_metrics(tree,obj,aes_resource->metrics.wchar,aes_resource->metrics.hchar,aes_resource->metrics.width)
/* The library exposes the classic file API, not rs_loadmem's internal path. */
#define rs_fixit(global) aes_resource_fix()
#define rs_loadmem aes_resource_loadmem
#endif
