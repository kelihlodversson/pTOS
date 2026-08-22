# EmuCON Ctrl-Arrow Editing Design

## Goal

Change EmuCON word-wise command-line navigation from Shift+Left/Right to
Ctrl+Left/Right, matching upstream EmuTOS commit `126abe62`.

## Behavior

Add invariant Ctrl+arrow scan-code definitions for left (`0x7300`) and right
(`0x7400`) navigation.  In `cli/cmdedit.c`, these codes select the existing
word-boundary movement paths; unmodified arrows retain one-character movement.

Shift+Left/Right are no longer special line-editing keys.  This deliberately
matches upstream behavior rather than preserving a pTOS compatibility alias.
The word-boundary algorithms and all other editing commands remain unchanged.

## User Interface

Update EmuCON's `HELP EDIT` entry from `shift-left/right arrow = previous/next
word` to `control-left/right arrow = previous/next word`.  The normal NLS
workflow regenerates the message template.  Update the eight existing pTOS
catalogs (`cs`, `de`, `es`, `fi`, `fr`, `gr`, `it`, and `ru`) with upstream's
translation for the new message; the deferred locale expansion in #240 is out
of scope.

## Verification

Build a configured image after the change.  Verify that the generated NLS
tables include the new help string and do not report it as untranslated; the
pre-existing deferred warnings from #240 may remain.  In an EmuCON session,
verify ordinary Left/Right movement and Ctrl+Left/Right word movement on a
multi-word command line; Shift+Left/Right must not take the word-movement path.

## Non-Goals

This does not add resolution switching, alter pTOS's line-height control, or
change command-history, tab-completion, or word-boundary semantics.
