/*
 * poweroff.c - Shutdown(SHUT_POWER), for the QEMU machines that can do it
 *
 * Not a suite of runtests.tos (the machine does not come back): a program of
 * its own, POWEROFF.TOS.  tools/run-regression-tests.sh starts it after the
 * suite and expects QEMU to exit by itself afterwards, which it does when the
 * machine's power off works (x86-64 ACPI, ARM PSCI, m68k virt-ctrl).
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>

int main(void)
{
    long rc;

    Cconws("shutdown: PASS (powering off)\r\n");
    rc = Shutdown(0L);
    /* only a machine without a power off gets here, after the message */
    Cconws("shutdown: FAIL (it returned)\r\n");
    return (int)rc;
}
