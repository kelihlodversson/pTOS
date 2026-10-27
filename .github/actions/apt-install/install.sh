#!/bin/bash
#
# install.sh - install apt packages from a cache of their .deb files if there
#              is one, else from the mirror (and leave the files in the cache)
#
# Usage: install.sh DIR [apt-get options] PACKAGE...
#
# DIR is the directory the action restored from the cache, or an empty one.
# Packages the runner image has already are not touched.  See action.yml.
#

set -euo pipefail

dir=$1
shift
opts=()
pkgs=()
for arg in "$@"; do
  case $arg in
  -*) opts+=("$arg") ;;
  *) pkgs+=("$arg") ;;
  esac
done

# "dpkg -s" also succeeds for a package that was removed (its configuration
# files are left) or never configured.
installed() {
  local p
  for p in "${pkgs[@]}"; do
    [ "$(dpkg-query -W -f='${Status}' "$p" 2>/dev/null)" = "install ok installed" ] ||
      return 1
  done
}

if installed; then
  echo "already installed: ${pkgs[*]}"
  exit 0
fi

# A mirror that stalls fails over (and is tried again) instead of crawling at
# 50 kB/s for minutes.
printf '%s\n' 'Acquire::Retries "5";' \
  'Acquire::http::Timeout "30";' 'Acquire::https::Timeout "30";' \
  'APT::Keep-Downloaded-Packages "true";' |
  sudo tee /etc/apt/apt.conf.d/80-ci >/dev/null

# The files of the packages and of everything they needed that the runner
# image lacks, as the last run on this image downloaded them: no index to
# fetch and nothing to download.
if compgen -G "$dir/*.deb" >/dev/null; then
  if sudo dpkg -i "$dir"/*.deb && installed; then
    echo "installed from the cache: ${pkgs[*]}"
    exit 0
  fi
  echo "::warning::the cached packages did not install, fetching them"
fi

sudo mkdir -p "$dir/partial"
# Error-Mode=any: an index that failed to download is an error, not a warning
# that "apt-get update" survives with an old index (and exit status 0).
for attempt in 1 2 3; do
  sudo apt-get update -qq -o APT::Update::Error-Mode=any && break
  [ "$attempt" -lt 3 ] || exit 1
  sleep 10
done
sudo apt-get install -y ${opts[@]+"${opts[@]}"} \
  -o Dir::Cache::archives="$dir" "${pkgs[@]}"

# The cache is packed by the runner's user: apt's lock file is not readable
# by it, and the files are root's.
sudo rm -rf "$dir/partial" "$dir/lock"
sudo chown -R "$(id -u):$(id -g)" "$dir"
