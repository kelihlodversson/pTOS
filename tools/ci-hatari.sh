#!/bin/bash
#
# ci-hatari.sh - run the regression tests under Hatari, several at a time
#
# Usage: tools/ci-hatari.sh [CONFIG:MODE...]      (EVENT=pull_request or not)
#
# For the products/CONFIG/system.img and test-hd.img that the Hatari workflow
# built: tools/run-hatari-tests.py in every combination of configuration and
# screen mode, one Hatari each, as many at a time as the machine has CPUs
# (Hatari's clock is the emulated one: the load does not change the results).
# Without arguments, a pull request (EVENT=pull_request) runs the
# combinations chosen from what depends on the mode and on the kernel
# (doc/ci.txt), anything else all of them.  Output goes to logs/ and, on GitHub
# Actions, to the job's summary, with an annotation for each that failed.
# Hatari and python3 have to be installed (CI: the 2.6 of an Ubuntu 26.04).
#

set -e -o pipefail

if [ $# -gt 0 ]; then
  combos="$*"
elif [ "$EVENT" = pull_request ]; then
  combos="atari512:low atari512:high atari512:f030 atari512:f040 atari512-fastcall:f040 atari512-kproc:f030 falcon-mmu:f030 falcon-aspace:f030a falcon-aspace:tta falcon-aspace040:f040a"
else
  combos="atari512:low atari512:med atari512:high atari512:f030 atari512:f040 atari512-fastcall:low atari512-fastcall:med atari512-fastcall:high atari512-fastcall:f030 atari512-fastcall:f040 atari512-extdesk:low atari512-extdesk:med atari512-extdesk:high atari512-extdesk:f030 atari512-extdesk:f040 atari512-kproc:low atari512-kproc:med atari512-kproc:high atari512-kproc:f030 atari512-kproc:f040 falcon-mmu:f030 falcon-aspace:f030a falcon-aspace:tta falcon-aspace040:f040a"
fi
summary=${GITHUB_STEP_SUMMARY:-/dev/null}
mkdir -p logs
run_one() {
  config=${1%%:*}
  mode=${1#*:}
  start=$(date +%s)
  if timeout 1500 python3 tools/run-hatari-tests.py \
       --tos "products/$config/system.img" \
       --disk "products/$config/test-hd.img" \
       --mode "$mode" --timeout 1200 >"logs/$config-$mode.log" 2>&1; then
    echo ok >"logs/$config-$mode.result"
  else
    echo FAILED >"logs/$config-$mode.result"
  fi
  echo $(($(date +%s) - start)) >"logs/$config-$mode.seconds"
}
export -f run_one
printf '%s\n' $combos | xargs -P "$(nproc)" -I{} bash -c 'run_one {}'

failed=
{
  echo "| configuration | mode | result | seconds |"
  echo "| --- | --- | --- | ---: |"
} >>"$summary"
for combo in $combos; do
  config=${combo%%:*}
  mode=${combo#*:}
  result=$(cat "logs/$config-$mode.result" 2>/dev/null || echo FAILED)
  seconds=$(cat "logs/$config-$mode.seconds" 2>/dev/null || echo "?")
  echo "::group::$config, $mode: $result ($seconds s)"
  cat "logs/$config-$mode.log"
  echo "::endgroup::"
  echo "| $config | $mode | $result | $seconds |" >>"$summary"
  if [ "$result" != ok ]; then
    failed="$failed $config,$mode"
    echo "::error title=$config, $mode::the tests failed on $config in the $mode mode"
  fi
done
if [ -n "$failed" ]; then
  echo "::error::failed:$failed"
  exit 1
fi
