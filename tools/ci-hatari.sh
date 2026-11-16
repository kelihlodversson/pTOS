#!/bin/bash
#
# ci-hatari.sh - build and run the regression tests under Hatari, several at
#                a time
#
# Usage: tools/ci-hatari.sh --group GROUP   run GROUP, building what is not
#                                           in products/ yet
#        tools/ci-hatari.sh --prepare CONFIG
#                                           in a tree that has just built CONFIG,
#                                           put its system and test disk into
#                                           products/CONFIG/ if a combination of
#                                           the event uses it
#        tools/ci-hatari.sh --list GROUP    print the combinations of GROUP
#        tools/ci-hatari.sh --check         check the groups (see below)
#        tools/ci-hatari.sh [CONFIG:MODE...]
#                                           run these, or with none every
#                                           combination of the event, on what
#                                           is in products/ already
#
# EVENT (or else GITHUB_EVENT_NAME) =pull_request selects the combinations of a
# pull request, anything else all of them; a push runs none, so --prepare does
# nothing for one.  A run is tools/run-hatari-tests.py on products/CONFIG/system.img
# and test-hd.img, in one screen mode, one Hatari each, as many at a time as
# the machine has CPUs (Hatari's clock is the emulated one: the load does not
# change the results, only how long they take).  Output goes to logs/ and, on
# GitHub Actions, to the job's summary, with an annotation for each that failed.
# Hatari and python3 have to be installed (CI: the pinned Hatari of
# tools/hatari-pin.sh), and the m68k toolchain for a --group that has to build.
#
# The combinations are in groups, one CI job each (the matrix of the hatari job
# of .github/workflows/build.yml), so that they run on several runners at once:
# a run takes from seconds to twenty minutes, and the job was a queue of them on
# the four CPUs of one runner.  A group of a pull request has at most four, so
# that they all start at once.  In CI the systems and their test disks come from
# the "build m68k-atari" job (tools/ci-build.sh runs --prepare, #987), so a
# group builds nothing; run locally, it builds what it needs.  What a pull request runs is chosen from what depends on the mode and on
# the kernel (doc/ci.txt).  --check fails when the groups of a pull request are
# not exactly PR_COMBOS, or the others not exactly ALL_COMBOS, or the groups
# are not those of the workflow's matrix.
#

set -e -o pipefail

EVENT=${EVENT:-${GITHUB_EVENT_NAME:-}}

PR_GROUPS="st variants mmu"
ALL_GROUPS="st fastcall kproc extdesk mmu"

PR_COMBOS="atari512:low atari512:high atari512:f030 atari512:f040 atari512-fastcall:f040 atari512-kproc:f030 falcon-mmu:f030 falcon-aspace:f030a falcon-aspace:tta falcon-aspace040:tt040a atari192:low"
ALL_COMBOS="atari512:low atari512:med atari512:high atari512:f030 atari512:f040 atari512-fastcall:low atari512-fastcall:med atari512-fastcall:high atari512-fastcall:f030 atari512-fastcall:f040 atari512-extdesk:low atari512-extdesk:med atari512-extdesk:high atari512-extdesk:f030 atari512-extdesk:f040 atari512-kproc:low atari512-kproc:med atari512-kproc:high atari512-kproc:f030 atari512-kproc:f040 falcon-mmu:f030 falcon-aspace:f030a falcon-aspace:tta falcon-aspace040:f040a falcon-aspace040:tt040a atari192:low atari192:med atari192:high"

# group_combos EVENT GROUP
group_combos() {
  case $1:$2 in
    pull_request:st)       echo "atari512:low atari512:high atari512:f030 atari512:f040" ;;
    pull_request:variants) echo "atari512-fastcall:f040 atari512-kproc:f030 atari192:low" ;;
    pull_request:mmu)      echo "falcon-mmu:f030 falcon-aspace:f030a falcon-aspace:tta falcon-aspace040:tt040a" ;;
    pull_request:*)        return 1 ;;
    *:st)       echo "atari512:low atari512:med atari512:high atari512:f030 atari512:f040" ;;
    *:fastcall) echo "atari512-fastcall:low atari512-fastcall:med atari512-fastcall:high atari512-fastcall:f030 atari512-fastcall:f040 atari192:low atari192:med atari192:high" ;;
    *:kproc)    echo "atari512-kproc:low atari512-kproc:med atari512-kproc:high atari512-kproc:f030 atari512-kproc:f040" ;;
    *:extdesk)  echo "atari512-extdesk:low atari512-extdesk:med atari512-extdesk:high atari512-extdesk:f030 atari512-extdesk:f040" ;;
    *:mmu)      echo "falcon-mmu:f030 falcon-aspace:f030a falcon-aspace:tta falcon-aspace040:f040a falcon-aspace040:tt040a" ;;
    *)          return 1 ;;
  esac
}

# event_combos: every combination of the event
event_combos() {
  case $EVENT in
    pull_request) echo "$PR_COMBOS" ;;
    push)         ;;
    *)            echo "$ALL_COMBOS" ;;
  esac
}

# prepare CONFIG, in a tree that has built CONFIG: products/CONFIG/system.img
# and test-hd.img
prepare() {
  config=$1
  case $config in
    atari192) image=ptos192us.img ;;   # -Os, LTO and -mfastcall, as the small ROMs
    *)        image=ptos512k.img ;;
  esac
  mkdir -p "products/$config"
  cp "$image" "products/$config/system.img"
  case $config in
    falcon-aspace|falcon-aspace040)
      # the harness is the shell of the system (#727): no EmuCON, no keyboard
      make test-hd-shell
      cp test-hd-shell.img "products/$config/test-hd.img"
      ;;
    *)
      make test-hd
      cp test-hd.img "products/$config/"
      ;;
  esac
}

# build CONFIG: build it here, then prepare it
build() {
  make distclean >/dev/null 2>&1
  make "${1}_defconfig"
  make
  prepare "$1"
}

# the sorted, one-per-line form of a list of combinations
sorted() { printf '%s\n' $* | sort; }

check() {
  status=0
  for event in pull_request schedule; do
    if [ $event = pull_request ]; then
      groups=$PR_GROUPS want=$PR_COMBOS
    else
      groups=$ALL_GROUPS want=$ALL_COMBOS
    fi
    got=
    for group in $groups; do
      got="$got $(group_combos $event "$group")" || { echo "error: no group $group for $event" >&2; status=1; }
    done
    if [ "$(sorted $got)" != "$(sorted $want)" ]; then
      echo "error: the groups of $event do not run exactly its combinations:" >&2
      diff <(sorted $want) <(sorted $got) | sed 's/^/  /' >&2 || true
      status=1
    fi
    if [ -n "$(sorted $got | uniq -d)" ]; then
      echo "error: a combination is in two groups of $event: $(sorted $got | uniq -d)" >&2
      status=1
    fi
  done
  # The matrix of the hatari job is these groups: it has to be exactly this
  # group: line (a list in a comment, or elsewhere, does not count).
  json() { j=$(printf '"%s",' $*); echo "[${j%,}]"; }
  want="group: \${{ fromJSON(github.event_name == 'pull_request' && '$(json $PR_GROUPS)' || '$(json $ALL_GROUPS)') }}"
  # (in the hatari: job, the first line under matrix: that is not a comment)
  lines=$(awk '/^  [a-z0-9_-]+:$/ { job = $1 } job == "hatari:" { print }' .github/workflows/build.yml |
          sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' |
          awk '/^matrix:$/ { m = 1; next } m && /^(#|$)/ { next } m { print; m = 0 }')
  if [ "$lines" != "$want" ]; then
    echo "error: the matrix of the hatari job of .github/workflows/build.yml is not the groups of this script; its group: line has to be" >&2
    echo "  $want" >&2
    echo "and is" >&2
    printf '  %s\n' "${lines:-(none)}" >&2
    status=1
  fi
  [ $status -eq 0 ] && echo "The Hatari groups run every combination once."
  return $status
}

case ${1:-} in
  --check)
    check
    exit
    ;;
  --list)
    group_combos "$EVENT" "$2" || { echo "no group $2" >&2; exit 2; }
    exit
    ;;
  --prepare)
    if printf '%s\n' $(event_combos) | sed 's/:.*//' | grep -qx -- "$2"; then
      prepare "$2"
    fi
    exit
    ;;
  --group)
    combos=$(group_combos "$EVENT" "$2") || { echo "no group $2" >&2; exit 2; }
    for config in $(printf '%s\n' $combos | sed 's/:.*//' | sort -u); do
      if [ -f "products/$config/system.img" ] && [ -f "products/$config/test-hd.img" ]; then
        echo "$config: the system and test disk of the build job"
      elif [ -n "${GITHUB_ACTIONS:-}" ]; then
        # the job has no toolchain: the build job did not make them
        echo "::error title=$config::products/$config has no system.img and test-hd.img: did \"build m68k-atari\" fail, or is $config not in it (.github/ci-configs)?"
        exit 1
      else
        echo "::group::build $config"
        build "$config"
        echo "::endgroup::"
      fi
    done
    ;;
  '')
    combos=$(event_combos)
    ;;
  *)
    combos="$*"
    ;;
esac
summary=${GITHUB_STEP_SUMMARY:-/dev/null}
mkdir -p logs
run_one() {
  config=${1%%:*}
  mode=${1#*:}
  start=$(date +%s)
  if timeout 2700 python3 tools/run-hatari-tests.py \
       --tos "products/$config/system.img" \
       --disk "products/$config/test-hd.img" \
       --mode "$mode" --timeout 2400 >"logs/$config-$mode.log" 2>&1; then
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
