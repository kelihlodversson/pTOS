/*
 * shutdown_prog.c - Shutdown() with a given mode, for the QEMU machines
 *
 * Not a suite of runtests.tos (the machine does not come back): built four
 * times, as POWEROFF.TOS, REBOOT.TOS, COLDBOOT.TOS and HALT.TOS (SHUT_MODE
 * and SHUT_WHAT from the Makefile).  tools/run-regression-tests.sh starts
 * them, each in a machine of its own.  QEMU runs without -no-reboot: it must
 * leave by itself after the power off (a reset would restart the machine
 * instead), and keep running after the two reboots (without the halt message,
 * and with a second EmuCON banner: the machine starts again) and after
 * the halt.
 *
 * Before it shuts down the program leaves a file open with data written and
 * not closed: a Shutdown() that does not close the files and write out the
 * buffers loses it, which the script finds out by looking at the disk
 * afterwards.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include <mint/osbind.h>
#include <mint/mintbind.h>

#define DATA_FILE   "C:\\SHUTDOWN.DAT"
#define DATA_SIZE   512

int main(void)
{
    char data[DATA_SIZE];
    long rc;
    int i;
    short fd;

    for (i = 0; i < DATA_SIZE; i++)
        data[i] = (char)(i * 7 + 3);
    fd = (short)Fcreate(DATA_FILE, 0);
    if (fd >= 0)
        Fwrite(fd, DATA_SIZE, data);    /* and no Fclose() */

    Cconws("shutdown: PASS (" SHUT_WHAT ")\r\n");
    rc = Shutdown((long)SHUT_MODE);
    /* only a machine that could not do it gets here, after the message */
    Cconws("shutdown: FAIL (it returned)\r\n");
    return (int)rc;
}
