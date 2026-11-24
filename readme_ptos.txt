pTOS - Portable TOS

This archive contains a pTOS build.  pTOS is an experimental, portable,
TOS-compatible operating system derived from EmuTOS.

For an explanation of pTOS's purpose, its relationship to EmuTOS, supported
hardware, and test status, see:

doc/ptos.txt
doc/status.txt

pTOS would not be possible without the substantial work of the EmuTOS
contributors.  pTOS is independently maintained: bugs in this build are the
responsibility of the pTOS team, not the EmuTOS development team.

Upstream EmuTOS information is available at:
https://github.com/emutos/emutos

For pTOS source code, build instructions, and issue tracking, visit:
https://github.com/kelihlodversson/pTOS

MMU fork compatibility: pTOS Pvfork uses private copy-on-write memory,
deliberately differing from FreeMiNT's shared-memory Pvfork. Public
Pfork/Pvfork (CONF_WITH_FORK) are enabled by default on eligible native MMU
builds with eager allocation or demand paging. These are x32, ARMv7 and the
68040 stack-ABI profile outside classic ROM-window builds. A 68030 running
the falcon-aspace build returns EINVFN. See
 doc/freemint-compatibility.txt
for the precise memory and parent-suspension policy.
