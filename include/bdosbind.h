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

/* OS entry points implemented in util/miscasm.S */
extern long trap1(int, ...); /* Not reentrant! Do not call for Pexec() */
extern long trap1_pexec(short mode, const char *path, const char *tail, const char *env);

/*
 * Every argument below is cast to `long` before reaching trap1()'s `...`.
 * trap1() is genuinely variadic (no real parameter type for the compiler
 * to convert an argument to, unlike an ordinary prototyped call), so
 * without this, a caller passing a plain (32-bit) LONG/int expression --
 * `Fseek(-9, fh, mode)` rather than `Fseek(-9L, fh, mode)`, say -- gets
 * only C's default variadic argument promotion (int stays int), and the
 * x86-64 backend's 32-bit register write for that argument then
 * zero-extends it into the corresponding 64-bit register by ordinary ISA
 * rules (any 32-bit destination write clears the upper 32 bits): -9
 * arrives as 0x00000000FFFFFFF7, not the sign-extended
 * 0xFFFFFFFFFFFFFFF7 a real negative long needs to reconstruct as. An
 * explicit `(long)` cast here makes the C compiler perform the
 * conversion at each call site instead, correctly (sign-extending a
 * signed scalar, and losslessly reinterpreting a pointer -- `long` is
 * this arch's native pointer width), regardless of what type the
 * caller's own literal or variable happens to be.
 *
 * A no-op on m68k/ARM, where trap1() reads these back off a stack frame
 * built to the m68k calling convention (or, on ARM, packs them into
 * registers) rather than forwarding raw variadic argument registers, and
 * where long/LONG are already the same width as the real argument in
 * every case here -- but worth applying uniformly rather than only
 * where it currently matters, since it costs nothing and this exact
 * class of bug is what #350's review caught on x86-64.
 */
#define Crawio(w) trap1(0x06, (long)(w))
#define Crawcin() trap1(0x07)
#define Cconws(buf) trap1(0x09, (long)(buf))
#define Cconis() trap1(0x0b)
#define Dsetdrv(drv) trap1(0x0e, (long)(drv))
#if CONF_WITH_VIDEL
#define Srealloc(amount) trap1(0x15, (long)(amount))
#endif
#define Dgetdrv() trap1(0x19)
#define Fsetdta(buf) trap1(0x1a, (long)(buf))
#define Fgetdta() trap1(0x2f)
#define Dcreate(path) trap1(0x39, (long)(path))
#define Dfree(buf,driveno) trap1(0x36, (long)(buf), (long)(driveno))
#define Ddelete(path) trap1(0x3a, (long)(path))
#define Dsetpath(path) trap1(0x3b, (long)(path))
#define Fcreate(fname,attr) trap1(0x3c, (long)(fname), (long)(attr))
#define Fopen(fname,mode) trap1(0x3d, (long)(fname), (long)(mode))
#define Fclose(handle) trap1(0x3e, (long)(handle))
#define Fread(handle,count,buf) trap1(0x3f, (long)(handle), (long)(count), (long)(buf))
#define Fwrite(handle,count,buf) trap1(0x40, (long)(handle), (long)(count), (long)(buf))
#define Fdelete(fname) trap1(0x41, (long)(fname))
#define Fseek(offset,handle,seekmode) trap1(0x42, (long)(offset), (long)(handle), (long)(seekmode))
#define Fattrib(filename,wflag,attrib) trap1(0x43, (long)(filename), (long)(wflag), (long)(attrib))
#define Mxalloc(amount,mode) trap1(0x44, (long)(amount), (long)(mode))
#define Dgetpath(path,driveno) trap1(0x47, (long)(path), (long)(driveno))
#define Malloc(number) trap1(0x48, (long)(number))
#define Mfree(block) trap1(0x49, (long)(block))
#define Mshrink(block,newsiz) trap1(0x4a, 0, (long)(block), (long)(newsiz))
#define Pexec(mode,name,cmdline,env) trap1_pexec(mode, name, cmdline, env)
#define Pterm0() trap1(0x00)
#define Fsfirst(filename,attr) trap1(0x4e, (long)(filename), (long)(attr))
#define Fsnext() trap1(0x4f)
#define Frename(oldname,newname) trap1(0x56, 0, (long)(oldname), (long)(newname))
#define Fdatime(timeptr,handle,wflag) trap1(0x57, (long)(timeptr), (long)(handle), (long)(wflag))

#endif /* _BDOSBIND_H */
