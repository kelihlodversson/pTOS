#!/bin/sh
#
# check-build-warnings.sh - fail if a build log contains any warning
#
# Usage: tools/check-build-warnings.sh BUILD-LOG
#
# Every configuration CI builds is warning-free (#584), so any line with
# "warning:" in it, in any case -- from the compiler, the assembler (which
# says "Warning:"), the linker, Kconfig or the Makefile's own checks -- is
# new.  On GitHub Actions each one also becomes an annotation on the PR.

log=$1
if [ -z "$log" ] || [ ! -r "$log" ]; then
    echo "usage: $0 BUILD-LOG" >&2
    exit 2
fi

warnings=$(grep -i 'warning:' "$log")
if [ -z "$warnings" ]; then
    echo "No build warnings."
    exit 0
fi

count=$(printf '%s\n' "$warnings" | wc -l)
printf '%s\n' "$warnings"
if [ -n "$GITHUB_ACTIONS" ]; then
    printf '%s\n' "$warnings" | while IFS= read -r line; do
        echo "::warning::$line"
    done
fi
echo "error: the build printed $count warning(s); see above." >&2
exit 1
