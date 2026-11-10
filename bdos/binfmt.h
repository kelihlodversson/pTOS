/* Boot-configured executable interpreters (#944). */
#ifndef BINFMT_H
#define BINFMT_H

#include "portab.h"

#define BINFMT_RULES 8
#define BINFMT_NAME 32
#define BINFMT_KEYS 16
#define BINFMT_KEY 64
#define BINFMT_LINE 512
#define BINFMT_MAGIC 32
#define BINFMT_HEADER 4096
#define BINFMT_INTERPRETER 260
#define BINFMT_SHEBANG 256
#define BINFMT_PATH 1024
#define BINFMT_DEPTH 4
#define BINFMT_ENV 32766UL
#define BINFMT_ARGC 16384UL
#define BINFMT_ENV_WORK (BINFMT_ENV + 2 + BINFMT_ARGC / 8)

typedef struct {
    char name[BINFMT_NAME];
    char interpreter[BINFMT_INTERPRETER];
    ULONG offset;
    UBYTE magic[BINFMT_MAGIC], mask[BINFMT_MAGIC];
    UWORD length, mask_length, seen;
    BOOL enabled, valid;
} BINFMT_RULE;

typedef struct {
    WORD bootdrive;
    UWORD count;
    BINFMT_RULE rules[BINFMT_RULES];
} BINFMT_REGISTRY;

typedef void (*BINFMT_DIAGNOSTIC)(ULONG line, const char *message, void *argument);

typedef struct {
    BINFMT_REGISTRY *registry;
    BINFMT_RULE *current;
    BINFMT_DIAGNOSTIC diagnostic;
    void *argument;
    ULONG section_line;
    int section, key_count;
    char keys[BINFMT_KEYS][BINFMT_KEY];
} BINFMT_PARSER;

typedef struct {
    char interpreter[BINFMT_INTERPRETER];
    char argument[BINFMT_SHEBANG + 1];
    BOOL has_argument;
} BINFMT_SELECTION;

/* Pure, bounded parsing/matching; no filesystem, allocation or host ABI. */
void binfmt_parser_init(BINFMT_PARSER *parser, BINFMT_REGISTRY *registry,
                       WORD bootdrive, BINFMT_DIAGNOSTIC diagnostic, void *argument);
void binfmt_config_line(BINFMT_PARSER *parser, char *line, ULONG length,
                       BOOL overlong, ULONG number);
void binfmt_config_end(BINFMT_PARSER *parser);
LONG binfmt_absolute(WORD bootdrive, const char *path, char *result);
LONG binfmt_select(const BINFMT_REGISTRY *registry, const UBYTE *header,
                   ULONG length, BINFMT_SELECTION *selection);
LONG binfmt_arguments(const char *environment, const char *tail,
                      const BINFMT_SELECTION *selection, const char *program,
                      char *storage, char output_tail[128]);

/* Kernel integration. Path points to argv[0] inside the generated environment.
 * The caller rebases it into the child's environment copy before releasing
 * that storage; the separate tail is released after copying the basepage. */
typedef struct {
    char *path, *tail, *environment;
} BINFMT_LAUNCH;

void binfmt_boot_init(void);
void binfmt_release_environment(BINFMT_LAUNCH *launch);
void binfmt_release(BINFMT_LAUNCH *launch);

#endif
