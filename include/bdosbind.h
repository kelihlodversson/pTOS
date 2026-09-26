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
 * TRAP1_ARG(x): on x86-64 only, cast an argument to `long` before it
 * reaches trap1()'s `...`. trap1() is genuinely variadic there (no real
 * parameter type for the compiler to convert an argument to, unlike an
 * ordinary prototyped call), so without this, a caller passing a plain
 * (32-bit) LONG/int expression -- `Fseek(-9, fh, mode)` rather than
 * `Fseek(-9L, fh, mode)`, say -- gets only C's default variadic argument
 * promotion (int stays int), and the x86-64 backend's 32-bit register
 * write for that argument then zero-extends it into the corresponding
 * 64-bit register by ordinary ISA rules (any 32-bit destination write
 * clears the upper 32 bits): -9 arrives as 0x00000000FFFFFFF7, not the
 * sign-extended 0xFFFFFFFFFFFFFFF7 a real negative long needs to
 * reconstruct as. An explicit `(long)` cast here makes the C compiler
 * perform the conversion at each call site instead, correctly
 * (sign-extending a signed scalar, and losslessly reinterpreting a
 * pointer -- `long` is this arch's native pointer width), regardless of
 * what type the caller's own literal or variable happens to be.
 *
 * NOT applied on m68k/ARM: unlike x86-64's software trap dispatch
 * (which reads back a uniform-width register-passed argument list),
 * m68k's trap1() (util/arch/m68k/miscasm.S) does no argument marshaling
 * of its own at all -- it saves the return address, executes `trap #1`
 * directly against the stack frame the *caller's* variadic call already
 * built, and the real GEMDOS trap handler reads each argument back off
 * that same frame at whatever width the caller's own C expression
 * naturally promoted to. Several GEMDOS opcodes have genuinely
 * WORD-sized (16-bit) argument slots in their real historical ABI (e.g.
 * Fseek's `handle`/`seekmode`) -- which is exactly what an uncast
 * WORD-typed argument already promotes to under m68k's `-mshort` `int`.
 * Forcing every argument to `long` (32-bit) here would widen those
 * slots and misalign every argument after them in the trap frame, a
 * real regression on m68k this port's other targets still build for
 * (caught in review -- see PR #350's discussion of commit 19d681e).
 * ARM already gets the correct width for the same reason `long`/`LONG`
 * being equal there makes this a no-op, not because ARM's own trap1()
 * shares m68k's exact mechanism.
 */
#if defined(__x86_64__)
#define TRAP1_ARG(x) ((long)(x))
#else
#define TRAP1_ARG(x) (x)
#endif

#define Crawio(w) trap1(0x06, TRAP1_ARG(w))
#define Crawcin() trap1(0x07)
#define Cconws(buf) trap1(0x09, TRAP1_ARG(buf))
#define Cconis() trap1(0x0b)
#define Dsetdrv(drv) trap1(0x0e, TRAP1_ARG(drv))
#if CONF_WITH_VIDEL
#define Srealloc(amount) trap1(0x15, TRAP1_ARG(amount))
#endif
#define Dgetdrv() trap1(0x19)
#define Fsetdta(buf) trap1(0x1a, TRAP1_ARG(buf))
#define Fgetdta() trap1(0x2f)
#define Dcreate(path) trap1(0x39, TRAP1_ARG(path))
#define Dfree(buf,driveno) trap1(0x36, TRAP1_ARG(buf), TRAP1_ARG(driveno))
#define Ddelete(path) trap1(0x3a, TRAP1_ARG(path))
#define Dsetpath(path) trap1(0x3b, TRAP1_ARG(path))
#define Fcreate(fname,attr) trap1(0x3c, TRAP1_ARG(fname), TRAP1_ARG(attr))
#define Fopen(fname,mode) trap1(0x3d, TRAP1_ARG(fname), TRAP1_ARG(mode))
#define Fclose(handle) trap1(0x3e, TRAP1_ARG(handle))
#define Fread(handle,count,buf) trap1(0x3f, TRAP1_ARG(handle), TRAP1_ARG(count), TRAP1_ARG(buf))
#define Fwrite(handle,count,buf) trap1(0x40, TRAP1_ARG(handle), TRAP1_ARG(count), TRAP1_ARG(buf))
#define Fdelete(fname) trap1(0x41, TRAP1_ARG(fname))
#define Fseek(offset,handle,seekmode) trap1(0x42, TRAP1_ARG(offset), TRAP1_ARG(handle), TRAP1_ARG(seekmode))
#define Fattrib(filename,wflag,attrib) trap1(0x43, TRAP1_ARG(filename), TRAP1_ARG(wflag), TRAP1_ARG(attrib))
#define Mxalloc(amount,mode) trap1(0x44, TRAP1_ARG(amount), TRAP1_ARG(mode))
#define Dgetpath(path,driveno) trap1(0x47, TRAP1_ARG(path), TRAP1_ARG(driveno))
#define Malloc(number) trap1(0x48, TRAP1_ARG(number))
#define Mfree(block) trap1(0x49, TRAP1_ARG(block))
#define Mshrink(block,newsiz) trap1(0x4a, 0, TRAP1_ARG(block), TRAP1_ARG(newsiz))
#define Pexec(mode,name,cmdline,env) trap1_pexec(mode, name, cmdline, env)
#define Pterm0() trap1(0x00)
#define Fsfirst(filename,attr) trap1(0x4e, TRAP1_ARG(filename), TRAP1_ARG(attr))
#define Fsnext() trap1(0x4f)
#define Frename(oldname,newname) trap1(0x56, 0, TRAP1_ARG(oldname), TRAP1_ARG(newname))
#define Fdatime(timeptr,handle,wflag) trap1(0x57, TRAP1_ARG(timeptr), TRAP1_ARG(handle), TRAP1_ARG(wflag))

#endif /* _BDOSBIND_H */
