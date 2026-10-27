#!/bin/bash
#
# ci-x86_64-boot.sh - boot the pc-x86_64 image just built under QEMU and OVMF
#                     and check the serial log
#
# Usage: tools/ci-x86_64-boot.sh        (from the top of the tree, after a
#                                        build of one of the pc-x86_64 configs)
#
# This is the "Boot-test under QEMU/OVMF" step of the x86_64 job of
# .github/workflows/build.yml, a script so that the job can run it for
# several configurations in turn.  The exit status says whether it passed;
# the output is the serial log and what was wrong with it.
#

set -e

# a configuration before this one left its files
rm -rf esp boot.log boot-monitor.sock OVMF_VARS_4M.fd

mkdir -p esp/EFI/BOOT
cp pc-x86_64.efi esp/EFI/BOOT/BOOTX64.EFI
# EmuCON, the x32 program the kernel loads from the boot drive
cp cli/emucon.tos esp/EMUCON.TOS
# a real x32 ELF on the boot drive: the self-test runs it through
# Pexec() from a ring-3 process (nested launch), as EmuCON does
make CROSS_COMPILE= x32test x32-vdi-import-test x32-aes-import-test aes-contract-test
if grep -q '^CONF_WITH_X32_ACCESSORY_TEST=y' .config; then
  make CROSS_COMPILE= -j2 x32-accessory-test
  cp obj/x32/*.ACC esp/
fi
cp obj/x32/VDI*.TOS obj/x32/AES*.TOS esp/
cp obj/x32/x32probe.elf esp/X32AES.TOS
cp obj/x32/x32probe.elf esp/X32CB.TOS
cp x32hello.elf esp/X32HELLO.TOS
# The optional VDI self-test loads a second client while the first
# still owns a workstation, to check process isolation.
cp obj/x32/x32probe.elf esp/X32VDI.TOS
# the same program linked for another address, which Pexec() has to
# move into the image window and relocate
cp x32reloc.elf esp/X32RELOC.TOS
# ... and one linked for another address without relocations, which
# must be refused
cp x32noreloc.elf esp/X32NOREL.TOS
# ... and one with a 6 MiB bss, loaded by PE_LOAD straight into the
# caller's heap
cp x32big.elf esp/X32BIG.TOS
# ... and one cut short, whose loading fails halfway
head -c 2000 x32hello.elf > esp/X32TRUNC.TOS
cp /usr/share/OVMF/OVMF_VARS_4M.fd .

rm -f boot.log boot-monitor.sock
aes_input_pid=
if grep -q '^CONF_WITH_X32_ACCESSORY_TEST=y' .config; then
  python3 tests/x32_accessory/drive_input.py boot.log boot-monitor.sock &
  aes_input_pid=$!
fi
set +e
timeout 60 qemu-system-x86_64 -machine pc -m 256 -cpu qemu64,+pdpe1gb \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd \
  -drive format=raw,file=fat:rw:esp \
  -serial file:boot.log -display none \
  -monitor unix:boot-monitor.sock,server=on,wait=off
qemu_exit=$?
aes_input_exit=0
if [ -n "$aes_input_pid" ]; then
  wait "$aes_input_pid"
  aes_input_exit=$?
fi
set -e

echo "::group::Serial console log"
cat boot.log
echo "::endgroup::"

if [ "$aes_input_exit" != 0 ]; then
  echo "::error::typed AES PS/2 input regression failed"
  exit 1
fi
if [ "$qemu_exit" != 124 ]; then
  echo "::error::qemu-system-x86_64 exited with code $qemu_exit instead of running until the timeout killed it -- that means it never reached the shell (where it would otherwise wait for input forever)"
  exit 1
fi

# A triple fault (a page-table, GDT/IDT or ABI bug) makes OVMF
# silently reset the VM and retry, which shows up as the boot
# progress lines repeating -- so exactly one attempt is part of
# the pass signal, not just "did it get far enough".
entries=$(grep -c 'EFI entry reached' boot.log || true)
if [ "$entries" != 1 ]; then
  echo "::error::expected exactly one 'EFI entry reached' line, found $entries -- likely a reset/triple-fault loop"
  exit 1
fi

if grep -q '^panic:' boot.log; then
  echo "::error::the kernel panicked during boot"
  exit 1
fi

# The memory-management self-test (bios/machine/pc-x86_64/memtest.c,
# #362) repeats process create/exit and injected-failure cycles
# and requires every allocator to return to its starting state:
# a leaked mapping, page-table or backing page, KPROC record or
# kernel-heap block fails it.
if grep -q 'x86-64 memtest: FAIL' boot.log; then
  echo "::error::the boot-time memory-management self-test failed"
  exit 1
fi
if ! grep -q 'x86-64 memtest: PASS' boot.log; then
  echo "::error::the boot-time memory-management self-test never reported PASS"
  exit 1
fi
if ! grep -q 'x86-64 AES client: PASS' boot.log || ! grep -q 'x86-64 AES imports: PASS' boot.log; then
  echo "::error::AES clients, process coordination, imports or baseline refusal did not pass"
  exit 1
fi
if ! grep -q 'x86-64 user callbacks: PASS' boot.log; then
  echo "::error::typed callbacks, nesting, alternate stacks, faults or baseline refusal failed"
  exit 1
fi
if ! grep -q 'x86-64 VDI imports: PASS' boot.log; then
  echo "::error::VDI import resolution, isolation or baseline refusal did not pass"
  exit 1
fi
if grep -q '^CONF_WITH_X32_VDI=y' .config && ! grep -q 'x86-64 VDI client: PASS' boot.log; then
  echo "::error::the ring-3 VDI client, validation and ownership tests did not pass"
  exit 1
fi
if grep -q '^CONF_WITH_X32_VDI=y' .config && ! grep -q 'x86-64 VDI padded pitch: PASS' boot.log; then
  echo "::error::VDI initialization or drawing corrupted padded framebuffer rows"
  exit 1
fi
if grep -q '^CONF_WITH_X32_VDI=y' .config && ! grep -q 'x86-64 VDI coordinate bounds: PASS' boot.log; then
  echo "::error::VDI coordinate bounds or large-screen drawing checks did not pass"
  exit 1
fi

# The self-test also launches real ring-3 x32 processes
# (tests/x32_probe/): entry state and selectors, a GEMDOS call
# across the syscall boundary, fault containment, private pages.
# Its deliberate faults print "process fault:" lines BEFORE the
# PASS line; any such line after it is the built-in EmuCON itself
# faulting.
if ! grep -q '^CONF_WITH_X32_ACCESSORY_TEST=y' .config &&
   sed -n '/x86-64 memtest: PASS/,$p' boot.log | grep -q 'process fault:'; then
  echo "::error::the ring-3 EmuCON faulted"
  exit 1
fi

if ! grep -q 'x86-64 nested pexec: PASS' boot.log; then
  echo "::error::a ring-3 process could not run a child through Pexec() and resume"
  exit 1
fi

if ! grep -q 'x86-64 private heap: PASS' boot.log; then
  echo "::error::a loaded x32 program could not use its private Malloc() memory"
  exit 1
fi

if ! grep -q 'x86-64 private image: PASS' boot.log; then
  echo "::error::a new process did not get its image in the image window"
  exit 1
fi

if ! grep -q 'x86-64 unaligned relocation: PASS' boot.log; then
  echo "::error::a program linked for another address was not relocated correctly"
  exit 1
fi

if ! grep -q 'x86-64 pexec go gives back: PASS' boot.log; then
  echo "::error::PE_GO did not give the loaded program's pages back"
  exit 1
fi

if ! grep -q 'x86-64 pexec go gives back with a gap: PASS' boot.log; then
  echo "::error::PE_GO did not find the loaded program's pages after the child allocated below them"
  exit 1
fi

# pc-x86_64-eager is built without demand paging (#562): the Psetlimit and
# demand-paging probes, which need it, say SKIP, and the eager one runs instead
if grep -q '^# CONF_WITH_DEMAND_PAGING is not set$' .config; then
  if ! grep -q 'x86-64 no demand paging: PASS' boot.log; then
    echo "::error::without demand paging, a process's memory was not backed when handed out, or Malloc(-1) promised more"
    exit 1
  fi
else
  if ! grep -q 'x86-64 psetlimit: PASS' boot.log; then
    echo "::error::Psetlimit did not set, report or pass on the limits and the stack size"
    exit 1
  fi

  if ! grep -q 'x86-64 demand paging: PASS' boot.log; then
    echo "::error::a process's Malloc() memory and stack were not backed on first touch"
    exit 1
  fi
fi

if ! grep -q 'x86-64 pexec load big: PASS' boot.log; then
  echo "::error::PE_LOAD did not take a program bigger than the image window"
  exit 1
fi

if ! grep -q 'x86-64 pexec truncated: PASS' boot.log; then
  echo "::error::a failed load did not leave the loader in its own address space"
  exit 1
fi

if ! grep -q 'x86-64 ancestors: PASS' boot.log; then
  echo "::error::a ring-3 grandchild did not see its ancestors' basepages"
  exit 1
fi

if ! grep -q 'x86-64 nested fault: PASS' boot.log; then
  echo "::error::a ring-3 process did not survive its child's fault and resume"
  exit 1
fi

if ! grep -q 'x86-64 nested ptermres: PASS' boot.log; then
  echo "::error::a ring-3 process could not keep launching children that stay resident"
  exit 1
fi

if grep -q '^CONF_WITH_X32_ACCESSORY_TEST=y' .config; then
  python3 tests/x32_accessory/check_boot.py boot.log
  exit 0
fi

# EmuCON is an x32 program run in ring 3 -- not linked into the
# kernel -- loaded from the boot drive as EMUCON.TOS (the kernel
# says "EmuCON: ..." when it cannot), so its banner can only come
# from that process.
if grep -Eq '^EmuCON: (cannot|this system has no built-in shell)' boot.log; then
  echo "::error::the kernel could not start the ring-3 EmuCON"
  exit 1
fi
if ! grep -q 'Welcome to EmuCON2' boot.log; then
  echo "::error::never reached the EmuCON2 shell prompt"
  exit 1
fi

echo "Boot test passed: reached the ring-3 EmuCON2 shell, no reset loop, no panic."
