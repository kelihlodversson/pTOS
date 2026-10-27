#!/bin/bash
#
# ci-group.sh - run a command for each of several configurations in one CI job
#
# Usage: tools/ci-group.sh GROUP COMMAND [ARGS...]
#        tools/ci-group.sh --names "NAME NAME..." COMMAND [ARGS...]
#
# Runs COMMAND [ARGS...] NAME for each configuration of GROUP in
# .github/ci-configs (or for each of the names given), one after the other,
# in a process of its own.  A pull request leaves out those in
# .github/pr-skip-configs (GITHUB_EVENT_NAME=pull_request).
#
# CI used to have a job for each configuration; a configuration is a minute
# or so, the runner and the toolchain more than that, and the repository has
# only so many runners at a time.  So that a configuration is still its own
# check, one that fails does not stop the others: it is named in an
# annotation and in the job's summary, and the exit status at the end says
# that there was one.
#

if [ "$1" = --names ]; then
    names=$2
    shift 2
    skipped=
else
    group=$1
    shift
    names=$(awk -v g="$group" '$1 !~ /^#/ && $2 == g { print $1 }' .github/ci-configs)
    skipped=
    if [ "$GITHUB_EVENT_NAME" = pull_request ]; then
        skipped=$(sed -e 's/#.*//' -e 's/[[:space:]].*//' .github/pr-skip-configs | grep .)
    fi
fi
if [ -z "$names" ] || [ $# -eq 0 ]; then
    echo "usage: $0 GROUP COMMAND [ARGS...]  (a group with configurations)" >&2
    exit 2
fi

summary=${GITHUB_STEP_SUMMARY:-/dev/null}
failed=
{
    echo "| name | result |"
    echo "| --- | --- |"
} >>"$summary"

for name in $names; do
    if echo "$skipped" | grep -qx "$name"; then
        echo "| $name | not built by a pull request |" >>"$summary"
        continue
    fi
    echo "::group::$name"
    # (the command is a process of its own: its own set -e works, which it
    # would not in a subshell under this if)
    if "$@" "$name"; then
        result=ok
    else
        result=FAILED
        failed="$failed $name"
        echo "::error title=$name::$name failed"
    fi
    echo "::endgroup::"
    echo "| $name | $result |" >>"$summary"
done

if [ -n "$failed" ]; then
    echo "::error::failed:$failed"
    exit 1
fi
