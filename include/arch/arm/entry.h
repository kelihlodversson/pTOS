/*
 * entry.h - ARM user-process entry contract
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef ARM_ENTRY_H
#define ARM_ENTRY_H

/*
 * r0 is the BASEPAGE pointer at every ARM user-process entry.  r1 identifies
 * the entry kind so startup code can select the ordinary or accessory path.
 */
#define ARM_ENTRY_PROGRAM       0
#define ARM_ENTRY_ACCESSORY     1

/*
 * Desk accessories replace this temporary stack during startup.  It must be
 * retained in their loaded basepage allocation because pstart()/gotopgm()
 * enters them without proc_go()'s normal process stack setup.
 */
#define ARM_ACCESSORY_STARTUP_STACK_SIZE 1024UL

#endif /* ARM_ENTRY_H */
