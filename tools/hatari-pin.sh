# hatari-pin.sh - the Hatari the CI tests run on (#807), pinned
#
# Sourced by tools/build-hatari.sh and by the setup-hatari action (its hash is
# the key of the cache: changing a line here rebuilds the emulator).  Not
# "latest": a newer commit is a deliberate change of this file, after the
# tests have been seen to pass on it.
#
# Why not the 2.6.1 of the Ubuntu package: it lacks the 68030 MMU fixes of
# September 2026.  One of them (b8a973a8, "Fix 68030 MMU pipeline fill bug": a
# stage B opcode captured before an exception was dispatched at the vector's
# PC) panicked the lazy-page tests of falcon-aspace in the parallel CI run (#805).
# This commit is the main branch of 2026-10-29, which has all of them.

HATARI_URL=https://github.com/hatari/hatari.git
HATARI_COMMIT=23bc3060ebf0da348cc842e358c102ba88002b56

HATARI_CMAKE_OPTIONS="-DCMAKE_BUILD_TYPE=Release"

# the Debian/Ubuntu packages it builds with and runs on
HATARI_APT_PACKAGES="cmake g++ pkg-config git libsdl2-dev libpng-dev zlib1g-dev \
 libreadline-dev libsamplerate0-dev"
