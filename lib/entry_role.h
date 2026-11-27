/* Runtime launch role shared by system libraries. */
#ifndef PTOS_ENTRY_ROLE_H
#define PTOS_ENTRY_ROLE_H
#include <stdint.h>

#define PTOS_ENTRY_PROGRAM 0
#define PTOS_ENTRY_ACCESSORY 1

/* The runtime publishes the launcher-selected role before preinitializers. */
uint32_t __ptos_entry_type(void);
#endif
