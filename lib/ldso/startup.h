/* Initial standard ELF32 stack, independent of host pointers (#660). */
#ifndef LDSO_STARTUP_H
#define LDSO_STARTUP_H
#include "dynamic.h"
#include "launch.h"
#define DL_START_CAPACITY 131072
#define DL_AT_PTOS_BASEPAGE 0x5000
#define DL_AT_PTOS_BOOTDRIVE 0x5001
#define DL_AT_PTOS_LOADBIAS 0x5002

typedef struct {
    uint32_t phdr, phnum, entry, base, pagesz, basepage, bootdrive, load_bias, capture_launch, environment;
} DL_START_INFO;

/* Construct argc/argv/envp/auxv and private strings in a caller-owned buffer
 * representing [top-capacity,top). Output RSP is 16-byte aligned. All inputs
 * are bounded, immutable and disjoint from the buffer and outputs. On error
 * outputs are zero; buffer contents are unspecified. No allocation or syscall.
 * Short GEMDOS tails split on spaces/tabs; single/double quotes group text,
 * are removed, and preserve empty arguments. Backslashes remain literal.
 * An unmatched quote is invalid. ARGV= with tail length 127 supplies the
 * NUL-separated argument list (including argv[0]); the marker and everything
 * after it are omitted from envp. ARGV=NULL: extensions are unsupported.
 * With a short tail, ARGV= and its payload are omitted but not used as argv.
 * Without extended ARGV, argv[0] and AT_EXECFN are the supplied filename.
 * capture_launch reserves a DL_LAUNCH auxiliary record and copies the complete
 * 128-byte original tail and original environment address/extent before user code; the interpreter qualifies its
 * command before preinitializers. A zero flag retains the existing stack ABI.
 * The environment must be double-NUL terminated within envsize (<=32768).
 */
int dl_start_stack(unsigned char *buffer, uint32_t capacity, uint32_t top,
                    const char *filename, uint32_t filenamesize,
                    const unsigned char *tail, uint32_t tailsize,
                    const unsigned char *env, uint32_t envsize,
                    const DL_START_INFO *info, uint32_t *rsp, uint32_t *used);
#endif
