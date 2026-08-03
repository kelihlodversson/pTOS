/*
 * virtio_input_keytbl.h - evdev KEY_* to Atari IKBD scancode table
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_INPUT_KEYTBL_H
#define VIRTIO_INPUT_KEYTBL_H

#include "portab.h"

#define VIRTIO_INPUT_KEYTBL_SIZE 256

/* Indexed by evdev KEY_* code (linux/input-event-codes.h). 0 means "no
 * Atari scancode for this key" -- callers must treat that as a miss, not
 * press the resulting scancode 0 (not a valid IKBD code anyway).
 * Populated once by virtio_input_keytbl_init(); read-only after that. */
extern UBYTE virtio_input_keytbl[VIRTIO_INPUT_KEYTBL_SIZE];

void virtio_input_keytbl_init(void);

#endif /* VIRTIO_INPUT_KEYTBL_H */
