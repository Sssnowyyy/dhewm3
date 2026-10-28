#!/bin/sh
# Builds dhewm3.rpx for the Wii U.
# Needs devkitPro with: dkp-pacman -S wiiu-dev wiiu-sdl2
set -e

: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
export DEVKITPPC="$DEVKITPRO/devkitPPC"

# run from the repo root, no matter where this is called from
cd "$(dirname "$0")/../../.."

# CafeGLSL compiles the GLSL shaders in sys/wiiu/shaders/ (see CMakeLists.txt)
CAFEGLSL_VERSION=1.0.0-rc1
CAFEGLSL_SHA256=8bfd253ff82a6cd268a0df4a24eabb1da2324444816df81c34a8aa3f0aa24002
if [ ! -x build-wiiu/tools/cafeglsl/bin/glslcompiler ]; then
	case "$(uname -s)-$(uname -m)" in
		Darwin-arm64) ;;
		*) echo "Get CafeGLSL for your system from https://github.com/Exzap/CafeGLSL/releases and pass -DWIIU_GLSLCOMPILER=<path>"; exit 1 ;;
	esac
	mkdir -p build-wiiu/tools/cafeglsl
	curl -fsSL -o build-wiiu/tools/cafeglsl.tar.xz \
		"https://github.com/Exzap/CafeGLSL/releases/download/v$CAFEGLSL_VERSION/cafeglsl-$CAFEGLSL_VERSION-macos-arm64.tar.xz"
	echo "$CAFEGLSL_SHA256  build-wiiu/tools/cafeglsl.tar.xz" | shasum -a 256 -c -
	tar -xf build-wiiu/tools/cafeglsl.tar.xz -C build-wiiu/tools/cafeglsl --strip-components=1
fi

"$DEVKITPRO/portlibs/wiiu/bin/powerpc-eabi-cmake" -S neo -B build-wiiu \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	"$@"

cmake --build build-wiiu -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

echo
echo "Built: build-wiiu/dhewm3.rpx and build-wiiu/dhewm3.wuhb (for the Wii U menu with Aroma)"
echo "Copy one of them to sd:/wiiu/apps/dhewm3/, put your Doom 3 base/*.pk4 files (patched to 1.3.1)"
echo "and base/gamepad.cfg from this repo in sd:/wiiu/apps/dhewm3/base/"
