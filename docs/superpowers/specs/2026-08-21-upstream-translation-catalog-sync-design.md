# Upstream Translation Catalog Sync Design

## Goal

Eliminate untranslated-entry warnings emitted while pTOS generates
multi-language translation tables.  Use the current upstream EmuTOS catalogs
as the translation source and restore all locales upstream supports.

## Scope

The synchronized locale set is:

`ca`, `cs`, `de`, `es`, `fi`, `fr`, `gr`, `hu`, `it`, `nl`, `pl`, `ro`, `ru`,
and `tr`.

This restores the seven locales currently excluded by pTOS: Catalan,
Hungarian, Dutch, Polish, Romanian, and Turkish.  Their upstream character-set
definitions are restored in `po/LINGUAS` with the existing eight locales.

## Catalog Reconciliation

EmuTOS master commit `a084e52da9556baf1470b2ffc865af2377923d37` is the
authoritative source for translation text.  Import its `po/*.po` catalogs and
`po/LINGUAS` locale definitions.  Do not copy upstream source lists blindly: regenerate
`po/messages.pot` from pTOS's `po/POTFILES.in`, then normalize every catalog
with `tools/bug update`.

This retains an upstream translation whenever its `msgid` is present in pTOS.
Upstream-only entries become obsolete and do not enter the generated table.
pTOS-only entries remain explicit untranslated entries and are resolved during
the synchronization so the generator has no untranslated-entry warnings.

## Verification

Use Homebrew GNU Make (`gmake`), because the system GNU Make 3.81 cannot build
pTOS.  Rebuild the translation tool and run the translation-table generation
for all locales.  Confirm the output reports zero untranslated entries for
every language and inspect the generated table's locale count.  Build a
representative multi-language pTOS configuration to verify the generated C
source compiles and links.

## Non-Goals

This work does not update pTOS source code to match upstream desktop or console
features.  It does not modify translation tooling or change language-selection
runtime behavior.
