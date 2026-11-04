# amiberry-pin.sh - the Amiberry the Amiga tests run on (#737), pinned
#
# Sourced by tools/build-amiberry.sh and by the setup-amiberry action (its
# hash is the key of the cache: changing a line here rebuilds the emulator).
# Not "latest": a newer tag is a deliberate change of this file, after the
# Amiga tests have been seen to pass on it (doc/debugging.txt).
#
# v8.5.0 (2026-10-27) merges WinUAE 6100b17 (2026-10-13): the 68030 ATC
# cache-state fix and the 68040/68060 write fast-cache fix of 2026-10-12, the
# 68030 fault and restart fixes of September 2026, and the MMU fixes of June
# 2026.

AMIBERRY_URL=https://github.com/BlitterStudio/amiberry.git
AMIBERRY_TAG=v8.5.0
AMIBERRY_COMMIT=2a622d2674c4114d3600c4cda004c857d4878bdf

# no sound, no network, no JIT, no accelerator boards or other emulated PCs:
# only what boots the Amiga ROMs of pTOS and answers on the IPC socket
AMIBERRY_CMAKE_OPTIONS="-DCMAKE_BUILD_TYPE=Release -DBUNDLE_SDL=OFF \
 -DUSE_PCEM=OFF -DUSE_PPC=OFF -DUSE_QEMU_PPC=OFF -DUSE_LIBSERIALPORT=OFF \
 -DUSE_LIBENET=OFF -DUSE_PORTMIDI=OFF -DUSE_LIBMPEG2=OFF -DUSE_MPG123=OFF \
 -DUSE_VIDEOGRAB=OFF -DUSE_FFMPEG=OFF -DUSE_WEBP=OFF -DUSE_UAENET_PCAP=OFF \
 -DUSE_UAENET_TAP=OFF -DUSE_ZSTD=OFF -DUSE_OPENGL=OFF -DUSE_JIT=OFF"

# the Debian/Ubuntu packages it builds with
AMIBERRY_APT_PACKAGES="cmake g++ pkg-config git libsdl3-dev libsdl3-image-dev \
 libpng-dev libflac-dev libcurl4-openssl-dev nlohmann-json3-dev zlib1g-dev"
