/* Experimental standard-ELF client interface, independent of the legacy CRT. */
#ifndef PTOS_ELF_CLIENT_H
#define PTOS_ELF_CLIENT_H
#include <stddef.h>
#include <stdint.h>

extern char **environ;
extern uint32_t ptos_basepage;
int32_t ptos_write(int handle, const void *buffer, uint32_t length);
int atexit(void (*function)(void));
void exit(int status) __attribute__((noreturn));
void _Exit(int status) __attribute__((noreturn));

/* Startup implementation, called only by the matching dynamic CRT. */
void __ptos_client_init(uint32_t argc, char **argv, char **envp);
void __ptos_start(const uint32_t *stack, void (*finalizer)(void),
                    int (*main_function)(int, char **, char **)) __attribute__((noreturn));
#endif
