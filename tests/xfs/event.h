/* Test-only commands for the mounted XFS fixture. */
#ifndef XFS_TEST_EVENT_H
#define XFS_TEST_EVENT_H

#include <stdint.h>

#define XFS_TEST_EVENT_PATH "R:\\FIXTURE\\EVENT.BIN"
#define XFS_TEST_CONTROL_PATH "R:\\FIXTURE\\DATA.TXT"
#define XFS_TEST_ARM 0x7a00
#define XFS_TEST_SAMPLE_COUNT 0x7a01
#define XFS_TEST_CLOSE_COUNT 0x7a02
#define XFS_TEST_SCALAR 0x7a03
#define XFS_TEST_OPEN_FLAGS 0x7a04
#define XFS_TEST_INPUT UINT32_C(0x81234567)
#define XFS_TEST_REPLY UINT32_C(0x5e465300)

#endif
