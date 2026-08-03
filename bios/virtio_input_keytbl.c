/*
 * virtio_input_keytbl.c - evdev KEY_* to Atari IKBD scancode table
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "config.h"

#if CONF_WITH_VIRTIO_INPUT

#include "portab.h"
#include "string.h"
#include "virtio_input_keytbl.h"

UBYTE virtio_input_keytbl[VIRTIO_INPUT_KEYTBL_SIZE];

/* evdev KEY_* codes 1 (KEY_ESC) through 68 (KEY_F10) number the entire
 * main alphanumeric block -- letters, digits, punctuation, Tab, Enter,
 * Backspace, Space, both Shifts, Ctrl, Alt, Caps Lock, F1-F10 -- using
 * the exact same IBM PC XT Scan Code Set 1 numbering the Atari ST's own
 * IKBD scancodes are built on (confirmed against bios/keyb_us.h's
 * keytbl_us_norm[], which is indexed by Atari scancode: e.g. index 30 is
 * 'a', and evdev's KEY_A is also 30). So this range is a straight
 * identity mapping; only keys outside it need an explicit override. */
#define VIRTIO_INPUT_KEYTBL_IDENTITY_MAX 68

void virtio_input_keytbl_init(void)
{
    WORD i;

    memset(virtio_input_keytbl, 0, sizeof(virtio_input_keytbl));

    for (i = 1; i <= VIRTIO_INPUT_KEYTBL_IDENTITY_MAX; i++)
        virtio_input_keytbl[i] = (UBYTE)i;

    /* KEY_KPASTERISK (keypad *) is the one evdev code in the identity range
     * above whose Atari scancode (0x37) has a special, non-key meaning:
     * bios/ikbd.c reads it as "mouse button 3" and routes it to mousexvec()
     * instead of normal key handling. The numeric keypad is out of scope for
     * this driver, so drop this one code back to 0 (unmapped) rather than
     * letting it fire a phantom middle-click. */
    virtio_input_keytbl[55] = 0;

    /* Navigation cluster: evdev numbers these from the AT "E0-prefixed"
     * extended set, which doesn't line up with the identity block above.
     * Atari's IKBD has its own fixed codes for the same keys (see
     * bios/ikbd.c's private KEY_HOME/KEY_UPARROW/KEY_LTARROW/KEY_RTARROW/
     * KEY_DNARROW/KEY_INSERT/KEY_DELETE #defines -- duplicated here as
     * literals since those macros aren't exported via ikbd.h). */
    virtio_input_keytbl[102] = 0x47;   /* KEY_HOME   -> KEY_HOME    */
    virtio_input_keytbl[103] = 0x48;   /* KEY_UP     -> KEY_UPARROW */
    virtio_input_keytbl[105] = 0x4b;   /* KEY_LEFT   -> KEY_LTARROW */
    virtio_input_keytbl[106] = 0x4d;   /* KEY_RIGHT  -> KEY_RTARROW */
    virtio_input_keytbl[108] = 0x50;   /* KEY_DOWN   -> KEY_DNARROW */
    virtio_input_keytbl[110] = 0x52;   /* KEY_INSERT -> KEY_INSERT  */
    virtio_input_keytbl[111] = 0x53;   /* KEY_DELETE -> KEY_DELETE  */

    /* The ST keyboard has one physical Ctrl and one Alt key; map both
     * evdev left/right variants onto them (the identity loop above
     * already covered KEY_LEFTCTRL==29 and KEY_LEFTALT==56). */
    virtio_input_keytbl[97]  = 0x1d;   /* KEY_RIGHTCTRL -> KEY_CTRL */
    virtio_input_keytbl[100] = 0x38;   /* KEY_RIGHTALT  -> KEY_ALT  */

    /* Out of scope for this driver (see design doc): numeric keypad,
     * F11+, multimedia keys, Meta/Super. Left at 0 (unmapped, i.e.
     * KDEBUG-logged and dropped by virtio_input.c, not "scancode 0"). */
}

#endif /* CONF_WITH_VIRTIO_INPUT */
