/* Internal admission/indexing for standard GEM menu-bar trees.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#ifndef GEMMENUBAR_H
#define GEMMENUBAR_H
#include "portab.h"
#include "obdefs.h"
typedef struct { WORD title, menu; } MENU_BAR_ENTRY;
/* count bounds the caller-owned object array. A NULL output queries the
 * number of pairs. On invalid structure/insufficient capacity return zero
 * without modifying output. Output storage must not overlap the tree. */
WORD mn_bar_index(const OBJECT *tree, WORD count, MENU_BAR_ENTRY *output, WORD capacity);
/* scratch needs (count+7)/8 bytes and must not overlap the tree. Admission
 * accepts classic string/title leaves; hidden payloads are not dereferenced.
 * Returns the pair count or zero. Only scratch is modified. */
WORD mn_bar_admit(const OBJECT *tree, WORD count, UBYTE *scratch, ULONG bytes);
#endif
