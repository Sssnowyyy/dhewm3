#!/bin/sh
# Builds dhewm3.rpx for the Wii U.
# Needs devkitPro with: dkp-pacman -S wiiu-dev wiiu-sdl2
set -e

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export DEVKITPPC="$DEVKITPRO/devkitPPC"

# run from the repo root, no matter where this is called from
cd "$(dirname "$0")/../../.."

"$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-cmake" -S neo -B build-wiiu \
	-DCMAKE_BUILD_TYPE=Release \
	"$@"

cmake --build build-wiiu -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

echo
echo "Built: build-wiiu/dhewm3.rpx"
echo "Copy it to sd:/wiiu/apps/dhewm3/ and put your Doom 3 base/*.pk4 files in sd:/wiiu/apps/dhewm3/base/"
