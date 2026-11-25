#ifndef REGION_SELFTEST_H
#define REGION_SELFTEST_H
/* Existing EXIT_HANDOFF_TEST/X86_64_MEMTEST gates only; no syscall or execution-policy change. */
void kproc_region_selftest(void);
#if CONF_WITH_ELF_VECTOR_ENTRY_TEST
void kproc_static_elf_selftest(void);
#endif
#endif
