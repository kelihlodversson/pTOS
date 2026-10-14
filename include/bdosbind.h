/*
 * bdosbind.h - Bindings for BDOS system calls
 *
 * Copyright (C) 2019-2022 The EmuTOS development team
 *
 * Authors:
 *  VRI   Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef _BDOSBIND_H
#define _BDOSBIND_H

#include "bdosdefs.h"
/* trap1_pexec(), and either trap1() (ARM) or trap1_v()/trap1_w()/...
 * (m68k) -- see asm.h itself for why GEMDOS calls need typed per-width
 * bindings on m68k but not on ARM. */
#include "asm.h"

#ifdef __arm__

/* Not reentrant! Do not call trap1() for Pexec() -- use Pexec() below,
 * which goes through the reentrant trap1_pexec() instead. */

#define Crawio(w) trap1(0x06, w)
#define Crawcin() trap1(0x07)
#define Cconws(buf) trap1(0x09, buf)
#define Cconis() trap1(0x0b)
#define Dsetdrv(drv) trap1(0x0e, drv)
#if CONF_WITH_VIDEL
#define Srealloc(amount) trap1(0x15, amount)
#endif
#define Dgetdrv() trap1(0x19)
#define Fsetdta(buf) trap1(0x1a, buf)
#define Fgetdta() trap1(0x2f)
#define Dcreate(path) trap1(0x39, path)
#define Dfree(buf,driveno) trap1(0x36, buf, driveno)
#define Ddelete(path) trap1(0x3a, path)
#define Dsetpath(path) trap1(0x3b, path)
#define Fcreate(fname,attr) trap1(0x3c, fname, attr)
#define Fopen(fname,mode) trap1(0x3d, fname, mode)
#define Fclose(handle) trap1(0x3e, handle)
#define Fread(handle,count,buf) trap1(0x3f, handle, count, buf)
#define Fwrite(handle,count,buf) trap1(0x40, handle, count, buf)
#define Fdelete(fname) trap1(0x41, fname)
#define Fseek(offset,handle,seekmode) trap1(0x42, offset, handle, seekmode)
#define Fattrib(filename,wflag,attrib) trap1(0x43, filename, wflag, attrib)
#define Mxalloc(amount,mode) trap1(0x44, amount, mode)
#define Dgetpath(path,driveno) trap1(0x47, path, driveno)
#define Malloc(number) trap1(0x48, number)
#define Mfree(block) trap1(0x49, block)
#define Mshrink(block,newsiz) trap1(0x4a, 0, block, newsiz)
#define Fsfirst(filename,attr) trap1(0x4e, filename, attr)
#define Fsnext() trap1(0x4f)
#define Frename(oldname,newname) trap1(0x56, 0, oldname, newname)
#define Fdatime(timeptr,handle,wflag) trap1(0x57, timeptr, handle, wflag)

#else /* m68k: typed trap1_* bindings from arch/m68k/asm.h -- see #300 */

#define Crawio(w) trap1_w(0x06, w)
#define Crawcin() trap1_v(0x07)
#define Cconws(buf) trap1_wl(0x09, buf)
#define Cconis() trap1_v(0x0b)
#define Dsetdrv(drv) trap1_w(0x0e, drv)
#if CONF_WITH_VIDEL
#define Srealloc(amount) trap1_wl(0x15, amount)
#endif
#define Dgetdrv() trap1_v(0x19)
#define Fsetdta(buf) trap1_wl(0x1a, buf)
#define Fgetdta() trap1_v(0x2f)
#define Dcreate(path) trap1_wl(0x39, path)
#define Dfree(buf,driveno) trap1_wlw(0x36, buf, driveno)
#define Ddelete(path) trap1_wl(0x3a, path)
#define Dsetpath(path) trap1_wl(0x3b, path)
#define Fcreate(fname,attr) trap1_wlw(0x3c, fname, attr)
#define Fopen(fname,mode) trap1_wlw(0x3d, fname, mode)
#define Fclose(handle) trap1_w(0x3e, handle)
#define Fread(handle,count,buf) trap1_wwll(0x3f, handle, count, buf)
#define Fwrite(handle,count,buf) trap1_wwll(0x40, handle, count, buf)
#define Fdelete(fname) trap1_wl(0x41, fname)
#define Fseek(offset,handle,seekmode) trap1_wlww(0x42, offset, handle, seekmode)
#define Fattrib(filename,wflag,attrib) trap1_wlww(0x43, filename, wflag, attrib)
#define Mxalloc(amount,mode) trap1_wlw(0x44, amount, mode)
#define Dgetpath(path,driveno) trap1_wlw(0x47, path, driveno)
#define Malloc(number) trap1_wl(0x48, number)
#define Mfree(block) trap1_wl(0x49, block)
#define Mshrink(block,newsiz) trap1_wwll(0x4a, 0, block, newsiz)
#define Fsfirst(filename,attr) trap1_wlw(0x4e, filename, attr)
#define Fsnext() trap1_v(0x4f)
#define Frename(oldname,newname) trap1_wwll(0x56, 0, oldname, newname)
#define Fdatime(timeptr,handle,wflag) trap1_wlww(0x57, timeptr, handle, wflag)

#endif /* __arm__ */

#define Pexec(mode,name,cmdline,env) trap1_pexec(mode, name, cmdline, env)

#endif /* _BDOSBIND_H */
