#!/bin/sh

set -eu

status=0

check_diff()
{
    label=$1
    shift

    if ! git diff "$@" --check -- .; then
        status=1
    fi

    if ! git diff "$@" --unified=0 --no-ext-diff -- '*.c' '*.h' '*.S' '*.awk' '*.sh' | \
        awk -v label="$label" '
            /^\+\+\+ b\// { file = substr($0, 7); next }
            /^\+\+\+ / { file = $0; next }
            /^\+[^+]/ {
                line = substr($0, 2)
                if (index(line, "\t")) {
                    printf "%s: %s: added line contains a tab\n", label, file
                    failed = 1
                }
                if (line ~ /\r$/) {
                    printf "%s: %s: added line contains CRLF\n", label, file
                    failed = 1
                }
            }
            END { exit failed ? 1 : 0 }
        '
    then
        status=1
    fi
}

check_diff staged --cached
check_diff unstaged

if [ $status -eq 0 ]; then
    echo 'gitready checks passed.'
fi

exit $status
