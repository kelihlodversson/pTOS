/*
 * virtio_input.h - virtio-input keyboard/mouse driver for the QEMU
 * virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H

#include "portab.h"

#if CONF_WITH_VIRTIO_INPUT

void virtio_input_init(void);

#endif /* CONF_WITH_VIRTIO_INPUT */

#endif /* VIRTIO_INPUT_H */
