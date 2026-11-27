/* GEMDOS polling ABI. Shared by the kernel and native userspace probes. */
#ifndef FPOLL_ABI_H
#define FPOLL_ABI_H

#define GEMDOS_FPOLL_TIME64 0x194
#define GEMDOS_FPOLL        0x15a
#define GEMDOS_FSELECT      0x11d

#define PTOS_POLLIN     0x001
#define PTOS_POLLPRI    0x002
#define PTOS_POLLOUT    0x004
#define PTOS_POLLERR    0x008
#define PTOS_POLLHUP    0x010
#define PTOS_POLLNVAL   0x020
#define PTOS_POLLRDNORM 0x040
#define PTOS_POLLRDBAND 0x080
#define PTOS_POLLWRNORM 0x100
#define PTOS_POLLWRBAND 0x200
#define PTOS_POLLMSG    0x400

#define PTOS_POLL_MAX 1024U

#ifndef __ASSEMBLER__
#include "portab.h"

typedef struct {
    LONG fd;
    UWORD events;
    UWORD revents;
} PTOS_POLLFD;
typedef char PTOS_POLLFD_ABI_SIZE[sizeof(PTOS_POLLFD) == 8 ? 1 : -1];
#endif
#endif
