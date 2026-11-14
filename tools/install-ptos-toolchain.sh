#!/bin/bash
# Install the published pTOS SDKs on Debian/Ubuntu or macOS.
set -euo pipefail

arch=${1:-all}
source=${2:-apt}
case $source in
    apt|--release) ;;
    *) echo "usage: $0 [m68k|arm|x86_64|all] [--release]" >&2; exit 2 ;;
esac
if [ "$#" -gt 2 ]; then
    echo "usage: $0 [m68k|arm|x86_64|all] [--release]" >&2
    exit 2
fi
case $arch in
    m68k) packages=(ptos-sdk-m68k); formulae=(ptos-m68k) ;;
    arm) packages=(ptos-sdk-arm); formulae=(ptos-arm) ;;
    x86_64) packages=(ptos-sdk-x86-64); formulae=(ptos-x86_64) ;;
    all)
        packages=(ptos-sdk-m68k ptos-sdk-arm ptos-sdk-x86-64)
        formulae=(ptos-m68k ptos-arm ptos-x86_64)
        ;;
    *) echo "usage: $0 [m68k|arm|x86_64|all] [--release]" >&2; exit 2 ;;
esac

case $(uname -s) in
    Linux)
        if [ "$(dpkg --print-architecture)" != amd64 ]; then
            echo 'The published APT SDKs require an amd64 host.' >&2
            exit 1
        fi
        admin=()
        if [ "$(id -u)" != 0 ]; then admin=(sudo); fi
        "${admin[@]}" apt-get update
        "${admin[@]}" apt-get install -y --no-install-recommends ca-certificates curl
        if [ "$source" = --release ]; then
            manifest=$(cd -- "$(dirname -- "$0")" && pwd)/ptos-toolchain.sha256
            version=$(sed -n 's/^# release: //p' "$manifest")
            if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
                echo 'Invalid release version in tools/ptos-toolchain.sha256' >&2
                exit 1
            fi
            scratch=$(mktemp -d)
            trap 'rm -rf -- "$scratch"' EXIT
            # APT's unprivileged downloader must be able to read local packages.
            chmod 0755 "$scratch"
            files=()
            for package in "${packages[@]}"; do
                file="${package}_${version}_amd64.deb"
                checksum=$(awk -v file="$file" '$2 == file { print $1 }' "$manifest")
                if [[ ! $checksum =~ ^[0-9a-f]{64}$ ]]; then
                    echo "Missing or invalid checksum for $file" >&2
                    exit 1
                fi
                curl --fail --silent --show-error --location --retry 3 \
                    "https://github.com/kelihlodversson/pTOS-toolchain/releases/download/v$version/$file" \
                    -o "$scratch/$file"
                chmod 0644 "$scratch/$file"
                printf '%s  %s\n' "$checksum" "$file" |
                    (cd -- "$scratch" && sha256sum --check -)
                files+=("$scratch/$file")
            done
            "${admin[@]}" apt-get install -y --no-install-recommends "${files[@]}"
        else
            key=$(mktemp)
            trap 'rm -f "$key"' EXIT
            curl --fail --silent --show-error --location --retry 3 \
                https://apt.ptos.dev/ptos-archive-keyring.gpg -o "$key"
            "${admin[@]}" install -m 0644 "$key" /usr/share/keyrings/ptos-archive-keyring.gpg
            printf '%s\n' 'deb [arch=amd64 signed-by=/usr/share/keyrings/ptos-archive-keyring.gpg] https://apt.ptos.dev stable main' |
                "${admin[@]}" tee /etc/apt/sources.list.d/ptos.list >/dev/null
            "${admin[@]}" apt-get update
            "${admin[@]}" apt-get install -y --no-install-recommends "${packages[@]}"
        fi
        dpkg-query -W "${packages[@]}"
        for package in "${packages[@]}"; do
            installed=$(dpkg-query -W -f='${Version}' "$package")
            if ! dpkg --compare-versions "$installed" ge 1.0.0; then
                echo "pTOS requires SDK 1.0.0 or newer; installed $package $installed" >&2
                exit 1
            fi
            if [ "$source" = --release ] && [ "$installed" != "$version" ]; then
                echo "$package: expected pinned version $version, installed $installed" >&2
                exit 1
            fi
        done
        ;;
    Darwin)
        if [ "$source" = --release ]; then
            echo '--release selects Debian packages; macOS uses the Homebrew tap.' >&2
            exit 2
        fi
        brew tap kelihlodversson/ptos-toolchain https://github.com/kelihlodversson/pTOS-toolchain.git
        for formula in "${formulae[@]}"; do
            if brew commands | grep -Fx trust >/dev/null; then
                brew trust --formula "kelihlodversson/ptos-toolchain/$formula"
            fi
            brew install "kelihlodversson/ptos-toolchain/$formula"
        done
        ;;
    *) echo 'Supported package managers are APT on Debian/Ubuntu and Homebrew on macOS.' >&2; exit 1 ;;
esac
