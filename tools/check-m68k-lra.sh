#!/bin/sh
# Kconfig default probe: only the standard pTOS GCC 16+ enables LRA.
cc=m68k-unknown-ptos-prgelf-gcc
if command -v "$cc" >/dev/null 2>&1 &&
   [ "$("$cc" -dumpmachine 2>/dev/null)" = m68k-unknown-ptos-prgelf ]; then
    version=$("$cc" -dumpversion 2>/dev/null)
    major=${version%%.*}
    case $major in
        ''|*[!0-9]*) ;;
        *)
            if [ "$major" -ge 16 ] &&
               printf 'void f(void){}\n' |
                   "$cc" -m68000 -msoft-float -mlra -x c -c -o /dev/null - >/dev/null 2>&1; then
                echo y
                exit 0
            fi
            ;;
    esac
fi
echo n
