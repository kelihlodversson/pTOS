#!/bin/bash
# Install the published pTOS SDKs on Debian/Ubuntu or macOS.
set -euo pipefail

arch=${1:-all}
case $arch in
    m68k) packages=(ptos-sdk-m68k); formulae=(ptos-m68k) ;;
    arm) packages=(ptos-sdk-arm); formulae=(ptos-arm) ;;
    x86_64) packages=(ptos-sdk-x86-64); formulae=(ptos-x86_64) ;;
    all)
        packages=(ptos-sdk-m68k ptos-sdk-arm ptos-sdk-x86-64)
        formulae=(ptos-m68k ptos-arm ptos-x86_64)
        ;;
    *) echo "usage: $0 [m68k|arm|x86_64|all]" >&2; exit 2 ;;
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
        key=$(mktemp)
        trap 'rm -f "$key"' EXIT
        curl --fail --silent --show-error --location --retry 3 \
            https://apt.ptos.dev/ptos-archive-keyring.gpg -o "$key"
        "${admin[@]}" install -m 0644 "$key" /usr/share/keyrings/ptos-archive-keyring.gpg
        printf '%s\n' 'deb [arch=amd64 signed-by=/usr/share/keyrings/ptos-archive-keyring.gpg] https://apt.ptos.dev stable main' |
            "${admin[@]}" tee /etc/apt/sources.list.d/ptos.list >/dev/null
        "${admin[@]}" apt-get update
        "${admin[@]}" apt-get install -y --no-install-recommends "${packages[@]}"
        dpkg-query -W "${packages[@]}"
        ;;
    Darwin)
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
